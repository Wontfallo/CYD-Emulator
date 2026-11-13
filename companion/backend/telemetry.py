from __future__ import annotations

import ctypes
import json
import os
import platform
import socket
import threading
import time
import urllib.request
import winreg
from typing import Any

import psutil
import win32gui
import win32pdh
import win32process

from .audio_win32 import detect_media_title, get_master_volume, get_mic_muted

try:
    import pynvml
    pynvml.nvmlInit()
    _HAS_NVML = True
except Exception:
    _HAS_NVML = False


def _read_cpu_brand() -> str:
    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
        ) as k:
            name, _ = winreg.QueryValueEx(k, "ProcessorNameString")
            cleaned = (
                name.replace("(R)", "")
                .replace("(TM)", "")
                .replace("Intel Core ", "Core ")
                .replace("CPU", "")
                .replace("14th Gen ", "")
                .replace("13th Gen ", "")
                .strip()
            )
            return " ".join(cleaned.split())[:23]
    except Exception:
        return platform.processor()[:23] or "Windows CPU"


def _read_gpu_brand_registry() -> str:
    cls_key = r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
    candidates: list[str] = []
    for idx in range(4):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, f"{cls_key}\\{idx:04d}") as k:
                desc, _ = winreg.QueryValueEx(k, "DriverDesc")
                s = str(desc).replace("(R)", "").replace("(TM)", "").replace("NVIDIA ", "").strip()
                if s and "Basic Display" not in s and "Remote" not in s:
                    candidates.append(s)
        except Exception:
            continue
    for c in candidates:
        if any(tag in c for tag in ("RTX", "GTX", "Radeon", "Arc")):
            return c[:27]
    return (candidates[0][:27] if candidates else "Integrated / Non-NVIDIA GPU")


class _MEMORYSTATUSEX(ctypes.Structure):
    _fields_ = [
        ("dwLength", ctypes.c_ulong),
        ("dwMemoryLoad", ctypes.c_ulong),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


def _read_commit_memory_gb() -> tuple[float, float]:
    try:
        ms = _MEMORYSTATUSEX()
        ms.dwLength = ctypes.sizeof(_MEMORYSTATUSEX)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(ms)):
            total_b = int(ms.ullTotalPageFile)
            avail_b = int(ms.ullAvailPageFile)
            used_b = max(0, total_b - avail_b)
            return round(used_b / (1024 ** 3), 1), round(total_b / (1024 ** 3), 1)
    except Exception:
        pass
    return 0.0, 0.0


def _get_active_window_exe() -> tuple[str, str]:
    try:
        hwnd = win32gui.GetForegroundWindow()
        if not hwnd:
            return "", ""
        title = win32gui.GetWindowText(hwnd) or ""
        _, pid = win32process.GetWindowThreadProcessId(hwnd)
        exe = psutil.Process(pid).name()
        return exe, title[:72]
    except Exception:
        return "", ""


class TelemetryCollector:
    def __init__(self):
        self._lock = threading.RLock()
        self.host_name = socket.gethostname()[:22]
        self.cpu_name = _read_cpu_brand()
        self.logical_cores = psutil.cpu_count(logical=True) or 1
        self.physical_cores = psutil.cpu_count(logical=False) or 1
        self._last_net = psutil.net_io_counters()
        self._last_net_ts = time.monotonic()
        self._last_disk_io = psutil.disk_io_counters()
        self._last_disk_ts = time.monotonic()
        self._lhm_check_ts = 0.0
        self._cached_cpu_temp: int = -1
        self._cached_cpu_pwr: int = -1
        self._nvml_owned = _HAS_NVML

        # PDH counters for real CPU turbo frequency on Windows (English counter locale-safe)
        self._pdh_query = None
        self._pdh_perf = None
        self._pdh_freq = None
        try:
            self._pdh_query = win32pdh.OpenQuery()
            add_fn = getattr(win32pdh, "AddEnglishCounter", win32pdh.AddCounter)
            self._pdh_perf = add_fn(
                self._pdh_query, r"\Processor Information(_Total)\% Processor Performance"
            )
            self._pdh_freq = add_fn(
                self._pdh_query, r"\Processor Information(_Total)\Processor Frequency"
            )
            win32pdh.CollectQueryData(self._pdh_query)
        except Exception:
            if self._pdh_query is not None:
                try:
                    win32pdh.CloseQuery(self._pdh_query)
                except Exception:
                    pass
            self._pdh_query = None

        self._gpu_handle = None
        self._gpu_name = "Integrated / Non-NVIDIA GPU"
        if _HAS_NVML:
            try:
                self._gpu_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
                raw_name = pynvml.nvmlDeviceGetName(self._gpu_handle)
                if isinstance(raw_name, bytes):
                    raw_name = raw_name.decode("utf-8", errors="ignore")
                self._gpu_name = raw_name.replace("NVIDIA ", "").strip()[:27]
            except Exception:
                self._gpu_handle = None

        self._gpu_pdh_query = None
        self._gpu_pdh_util = None
        self._gpu_pdh_vram = None
        if self._gpu_handle is None:
            self._gpu_name = _read_gpu_brand_registry()
            try:
                add_fn = getattr(win32pdh, "AddEnglishCounter", win32pdh.AddCounter)
                gq = win32pdh.OpenQuery()
                self._gpu_pdh_util = add_fn(gq, r"\GPU Engine(*engtype_3D*)\Utilization Percentage")
                self._gpu_pdh_vram = add_fn(gq, r"\GPU Adapter Memory(*)\Dedicated Usage")
                win32pdh.CollectQueryData(gq)
                self._gpu_pdh_query = gq
            except Exception:
                if self._gpu_pdh_query is not None:
                    try:
                        win32pdh.CloseQuery(self._gpu_pdh_query)
                    except Exception:
                        pass
                self._gpu_pdh_query = None
                self._gpu_pdh_util = None
                self._gpu_pdh_vram = None

        # Prime percpu & process counters cleanly
        psutil.cpu_percent(interval=None, percpu=True)
        for _ in psutil.process_iter(["name", "cpu_percent"]):
            pass
        time.sleep(0.15)

    def close(self) -> None:
        with self._lock:
            if self._pdh_query is not None:
                try:
                    win32pdh.CloseQuery(self._pdh_query)
                except Exception:
                    pass
                self._pdh_query = None
                self._pdh_perf = None
                self._pdh_freq = None
            if self._gpu_pdh_query is not None:
                try:
                    win32pdh.CloseQuery(self._gpu_pdh_query)
                except Exception:
                    pass
                self._gpu_pdh_query = None
                self._gpu_pdh_util = None
                self._gpu_pdh_vram = None
            if self._nvml_owned and _HAS_NVML:
                try:
                    pynvml.nvmlShutdown()
                except Exception:
                    pass
                self._nvml_owned = False
                self._gpu_handle = None

    def _read_boost_clk_mhz(self) -> int:
        if self._pdh_query is not None:
            try:
                win32pdh.CollectQueryData(self._pdh_query)
                _, perf = win32pdh.GetFormattedCounterValue(self._pdh_perf, win32pdh.PDH_FMT_DOUBLE)
                _, base_freq = win32pdh.GetFormattedCounterValue(self._pdh_freq, win32pdh.PDH_FMT_DOUBLE)
                mhz = int(round(base_freq * (perf / 100.0)))
                if mhz > 200:
                    return mhz
            except Exception:
                pass
        freq = psutil.cpu_freq()
        return int(round(freq.current)) if freq else 0

    def _probe_lhm_cpu_sensors(self) -> tuple[int, int]:
        """Non-blocking optional query to LibreHardwareMonitor web server (127.0.0.1:8085) if running."""
        now = time.monotonic()
        if now - self._lhm_check_ts < 5.0:
            return self._cached_cpu_temp, self._cached_cpu_pwr
        self._lhm_check_ts = now
        try:
            with urllib.request.urlopen("http://127.0.0.1:8085/data.json", timeout=0.08) as resp:
                tree = json.loads(resp.read().decode("utf-8", errors="ignore"))

            found_temp = -1
            found_pwr = -1

            def _walk(node: dict[str, Any], in_cpu: bool = False) -> None:
                nonlocal found_temp, found_pwr
                txt = str(node.get("Text", ""))
                is_cpu = in_cpu or ("Intel Core" in txt or "AMD Ryzen" in txt or "CPU Package" in txt)
                if is_cpu and "Package" in txt:
                    val = str(node.get("Value", ""))
                    if "°C" in val:
                        found_temp = int(round(float(val.split(" ")[0])))
                    elif " W" in val:
                        found_pwr = int(round(float(val.split(" ")[0])))
                for ch in node.get("Children", []) or []:
                    _walk(ch, is_cpu)

            _walk(tree)
            self._cached_cpu_temp = found_temp
            self._cached_cpu_pwr = found_pwr
        except Exception:
            self._cached_cpu_temp = -1
            self._cached_cpu_pwr = -1

        # Fallback: WMI LibreHardwareMonitor / OpenHardwareMonitor / ACPI ThermalZone
        if self._cached_cpu_temp < 0:
            try:
                import gc
                import pythoncom  # type: ignore
                import win32com.client  # type: ignore

                coinit_owned = False
                try:
                    pythoncom.CoInitializeEx(pythoncom.COINIT_MULTITHREADED)
                    coinit_owned = True
                except pythoncom.com_error as ce:
                    # RPC_E_CHANGED_MODE (-2147417850): COM already initialized on this thread in another mode
                    if ce.hresult == -2147417850:
                        coinit_owned = False
                    else:
                        raise

                def _query_wmi_sensors() -> tuple[int, int]:
                    t_val = -1
                    p_val = -1
                    wmi_svc = win32com.client.Dispatch("WbemScripting.SWbemLocator")
                    for ns in ("root\\LibreHardwareMonitor", "root\\OpenHardwareMonitor"):
                        try:
                            conn = wmi_svc.ConnectServer(".", ns)
                            rows = conn.ExecQuery(
                                "SELECT Name, SensorType, Value FROM Sensor "
                                "WHERE SensorType='Temperature' OR SensorType='Power'"
                            )
                            for row in rows:
                                nm = str(getattr(row, "Name", "") or "")
                                st = str(getattr(row, "SensorType", "") or "")
                                val = float(getattr(row, "Value", 0.0) or 0.0)
                                if st == "Temperature" and ("CPU Package" in nm or "Core Max" in nm or nm == "Core (Tctl/Tdie)"):
                                    if val > 0:
                                        t_val = int(round(val))
                                elif st == "Power" and "CPU Package" in nm and val > 0:
                                    p_val = int(round(val))
                            if t_val > 0:
                                return t_val, p_val
                        except Exception:
                            continue
                    try:
                        conn = wmi_svc.ConnectServer(".", "root\\wmi")
                        rows = conn.ExecQuery("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature")
                        for row in rows:
                            dk = float(getattr(row, "CurrentTemperature", 0) or 0)
                            deg_c = (dk / 10.0) - 273.15
                            if 15.0 <= deg_c <= 115.0:
                                t_val = max(t_val, int(round(deg_c)))
                    except Exception:
                        pass
                    return t_val, p_val

                try:
                    t_found, p_found = _query_wmi_sensors()
                    if t_found > 0:
                        self._cached_cpu_temp = t_found
                    if p_found > 0:
                        self._cached_cpu_pwr = p_found
                finally:
                    # Collect COM wrappers inside helper frame BEFORE uninitializing apartment
                    gc.collect()
                    if coinit_owned:
                        pythoncom.CoUninitialize()
            except Exception:
                pass
        return self._cached_cpu_temp, self._cached_cpu_pwr

    def _top_processes(self) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
        cpu_entries: list[tuple[float, str]] = []
        ram_entries: list[tuple[float, str]] = []
        ignore = {"system idle process", "system", "registry", "memory compression", "idle"}
        norm = max(1, self.logical_cores)

        for p in psutil.process_iter(["name", "cpu_percent", "memory_info"]):
            try:
                info = p.info
                pname = info.get("name") or ""
                if not pname or pname.lower() in ignore:
                    continue
                short = pname.replace(".exe", "")[:18]
                cpu_v = min(100.0, float(info.get("cpu_percent") or 0.0) / norm)
                mem_gb = int(getattr(info.get("memory_info"), "rss", 0) or 0) / (1024 ** 3)
                if cpu_v >= 0.1:
                    cpu_entries.append((cpu_v, short))
                ram_entries.append((mem_gb, short))
            except Exception:
                continue

        # Aggregate same process names (e.g., multiple chrome tabs) cleanly
        def _top_n(items: list[tuple[float, str]], n: int = 4) -> list[dict[str, Any]]:
            agg: dict[str, float] = {}
            for val, name in items:
                agg[name] = agg.get(name, 0.0) + val
            sorted_items = sorted(agg.items(), key=lambda x: x[1], reverse=True)[:n]
            return [{"name": k, "val": round(v, 1)} for k, v in sorted_items]

        return _top_n(cpu_entries, 4), _top_n(ram_entries, 4)

    def _downsample_cores(self, percpu: list[float], target: int = 16) -> list[int]:
        n = len(percpu)
        if n <= target:
            return [int(round(v)) for v in percpu]
        out: list[int] = []
        step = n / target
        for i in range(target):
            s = int(i * step)
            e = max(s + 1, int((i + 1) * step))
            out.append(int(round(max(percpu[s:e]))))
        return out

    def _collect_disks(self) -> list[dict[str, Any]]:
        vols = []
        seen = set()
        for part in psutil.disk_partitions(all=False):
            if "cdrom" in part.opts.lower() or not part.fstype:
                continue
            mp = part.mountpoint
            if mp in seen:
                continue
            seen.add(mp)
            try:
                du = psutil.disk_usage(mp)
                vols.append({
                    "mount": mp.rstrip("\\"),
                    "free_gb": round(du.free / (1024 ** 3), 1),
                    "total_gb": round(du.total / (1024 ** 3), 0),
                    "pct": int(round(du.percent)),
                })
            except Exception:
                continue
        return vols[:4]

    def sample(self) -> dict[str, Any]:
        with self._lock:
            return self._sample_locked()

    def _sample_locked(self) -> dict[str, Any]:
        percpu = psutil.cpu_percent(interval=None, percpu=True)
        cpu_pct = int(round(sum(percpu) / max(1, len(percpu))))
        cpu_clk = self._read_boost_clk_mhz()
        cpu_tmp, cpu_pwr = self._probe_lhm_cpu_sensors()
        top_cpu_list, top_ram_list = self._top_processes()

        top_cpu = top_cpu_list[0]["name"] if top_cpu_list else "-"
        top_cpu_val = top_cpu_list[0]["val"] if top_cpu_list else 0.0
        top_ram = top_ram_list[0]["name"] if top_ram_list else "-"
        top_ram_val = top_ram_list[0]["val"] if top_ram_list else 0.0

        vm = psutil.virtual_memory()
        ram_used_gb = round(vm.used / (1024 ** 3), 2)
        ram_total_gb = round(vm.total / (1024 ** 3), 1)
        ram_pct = int(round(vm.percent))
        commit_used_gb, commit_total_gb = _read_commit_memory_gb()
        try:
            sm = psutil.swap_memory()
            swap_used_gb = round(sm.used / (1024 ** 3), 1)
            swap_total_gb = round(sm.total / (1024 ** 3), 1)
        except Exception:
            swap_used_gb = 0.0
            swap_total_gb = 0.0

        now_ts = time.monotonic()
        net = psutil.net_io_counters()
        dt_net = max(0.05, now_ts - self._last_net_ts)
        ndn_mbps = round(((net.bytes_recv - self._last_net.bytes_recv) * 8.0) / (dt_net * 1e6), 2)
        nup_mbps = round(((net.bytes_sent - self._last_net.bytes_sent) * 8.0) / (dt_net * 1e6), 2)
        self._last_net = net
        self._last_net_ts = now_ts

        dio = psutil.disk_io_counters()
        dt_disk = max(0.05, now_ts - self._last_disk_ts)
        disk_r_mbps = round((dio.read_bytes - self._last_disk_io.read_bytes) / (dt_disk * 1024 * 1024), 1) if dio and self._last_disk_io else 0.0
        disk_w_mbps = round((dio.write_bytes - self._last_disk_io.write_bytes) / (dt_disk * 1024 * 1024), 1) if dio and self._last_disk_io else 0.0
        self._last_disk_io = dio
        self._last_disk_ts = now_ts

        sys_drive = (os.environ.get("SystemDrive", "C:").rstrip("\\") + "\\")
        disk = psutil.disk_usage(sys_drive)
        disk_free_gb = round(disk.free / (1024 ** 3), 1)
        disks = self._collect_disks()

        gpu_pct = 0
        gpu_temp = -1
        gpu_pwr = 0
        gpu_pmx = 0
        vram_used = 0
        vram_tot = 0
        gclk = 0
        gmck = 0
        gfan = 0
        gpu_available = False
        top_vram_proc = "-"
        if self._gpu_handle is not None:
            try:
                util = pynvml.nvmlDeviceGetUtilizationRates(self._gpu_handle)
                gpu_pct = int(util.gpu)
                gpu_temp = int(pynvml.nvmlDeviceGetTemperature(self._gpu_handle, pynvml.NVML_TEMPERATURE_GPU))
                mem = pynvml.nvmlDeviceGetMemoryInfo(self._gpu_handle)
                vram_used = int(mem.used // (1024 * 1024))
                vram_tot = int(mem.total // (1024 * 1024))
                gpu_pwr = int(round(pynvml.nvmlDeviceGetPowerUsage(self._gpu_handle) / 1000.0))
                gpu_pmx = int(round(pynvml.nvmlDeviceGetEnforcedPowerLimit(self._gpu_handle) / 1000.0))
                gclk = int(pynvml.nvmlDeviceGetClockInfo(self._gpu_handle, pynvml.NVML_CLOCK_GRAPHICS))
                gmck = int(pynvml.nvmlDeviceGetClockInfo(self._gpu_handle, pynvml.NVML_CLOCK_MEM))
                gpu_available = True
                try:
                    gfan = int(pynvml.nvmlDeviceGetFanSpeed(self._gpu_handle))
                except Exception:
                    gfan = 0
                try:
                    proc_vram: dict[str, int] = {}
                    for getter_name in (
                        "nvmlDeviceGetGraphicsRunningProcesses",
                        "nvmlDeviceGetComputeRunningProcesses",
                    ):
                        getter = getattr(pynvml, getter_name, None)
                        if getter is None:
                            continue
                        for pinfo in getter(self._gpu_handle) or []:
                            pid = int(getattr(pinfo, "pid", 0) or 0)
                            used_b = int(getattr(pinfo, "usedGpuMemory", 0) or 0)
                            if pid <= 0 or used_b <= 0 or used_b > (1 << 50):
                                continue
                            try:
                                pname = psutil.Process(pid).name().replace(".exe", "")[:14]
                            except Exception:
                                pname = f"pid:{pid}"
                            proc_vram[pname] = max(proc_vram.get(pname, 0), used_b // (1024 * 1024))
                    if proc_vram:
                        best_n, best_mb = max(proc_vram.items(), key=lambda kv: kv[1])
                        top_vram_proc = f"{best_n} {best_mb}M" if best_mb < 1000 else f"{best_n} {best_mb / 1024.0:.1f}G"
                except Exception:
                    pass
            except Exception:
                gpu_available = False
        elif self._gpu_pdh_query is not None:
            try:
                win32pdh.CollectQueryData(self._gpu_pdh_query)
                if self._gpu_pdh_util is not None:
                    u_arr = win32pdh.GetFormattedCounterArray(self._gpu_pdh_util, win32pdh.PDH_FMT_DOUBLE)
                    gpu_pct = max(0, min(100, int(round(sum(max(0.0, float(v)) for v in u_arr.values())))))
                    gpu_available = True
                if self._gpu_pdh_vram is not None:
                    v_arr = win32pdh.GetFormattedCounterArray(self._gpu_pdh_vram, win32pdh.PDH_FMT_LARGE)
                    if v_arr:
                        max_b = max(int(v) for v in v_arr.values())
                        vram_used = max(0, max_b // (1024 * 1024))
                        gpu_available = True
            except Exception:
                pass

        vol_pct, vol_muted = get_master_volume()
        mic_muted = get_mic_muted()
        media_playing, song = detect_media_title()
        active_exe, active_title = _get_active_window_exe()

        return {
            "host": self.host_name,
            "cpun": self.cpu_name,
            "gpun": self._gpu_name,
            "clk": time.strftime("%H:%M"),
            "cpu": cpu_pct,
            "cclk": cpu_clk,
            "ctmp": cpu_tmp,
            "cpwr": cpu_pwr,
            "cproc": f"{top_cpu} {top_cpu_val:.0f}%"[:18] if top_cpu != "-" else "-",
            "top_cpu_list": top_cpu_list,
            "cores": self._downsample_cores(percpu, 16),
            "all_cores": [int(round(v)) for v in percpu],
            "logical_cores": self.logical_cores,
            "physical_cores": self.physical_cores,
            "gpu": gpu_pct,
            "gpu_available": gpu_available,
            "gtmp": gpu_temp,
            "gpwr": gpu_pwr,
            "gpmx": gpu_pmx,
            "vram": vram_used,
            "vtot": vram_tot,
            "gclk": gclk,
            "gmck": gmck,
            "gfan": gfan,
            "top_vram_proc": top_vram_proc,
            "ram": ram_pct,
            "ruse": ram_used_gb,
            "rtot": ram_total_gb,
            "commit_used_gb": commit_used_gb,
            "commit_total_gb": commit_total_gb,
            "swap_used_gb": swap_used_gb,
            "swap_total_gb": swap_total_gb,
            "rproc": f"{top_ram} {top_ram_val:.1f}G"[:18] if top_ram != "-" else "-",
            "top_ram_list": top_ram_list,
            "ndn": max(0.0, ndn_mbps),
            "nup": max(0.0, nup_mbps),
            "dfre": disk_free_gb,
            "disk_r_mbps": max(0.0, disk_r_mbps),
            "disk_w_mbps": max(0.0, disk_w_mbps),
            "disks": disks,
            "vol": vol_pct,
            "vmute": vol_muted,
            "mmute": mic_muted,
            "mplay": media_playing,
            "song": song,
            "active_exe": active_exe,
            "active_title": active_title,
        }

    def to_device_packet(self, stats: dict[str, Any]) -> dict[str, Any]:
        keys = (
            "host", "cpun", "gpun", "clk", "cpu", "cclk", "ctmp", "cpwr", "cproc", "cores",
            "gpu", "gtmp", "gpwr", "gpmx", "vram", "vtot", "gclk", "gmck", "gfan",
            "ram", "ruse", "rtot", "rproc", "ndn", "nup", "dfre", "vol", "vmute",
            "mmute", "mplay", "song"
        )
        pkt = {"t": "stat"}
        for k in keys:
            if k in stats:
                pkt[k] = stats[k]
        if "gpu_available" in stats:
            pkt["gavail"] = 1 if stats["gpu_available"] else 0
        if "commit_used_gb" in stats:
            pkt["cmtu"] = stats["commit_used_gb"]
        if "commit_total_gb" in stats:
            pkt["cmtt"] = stats["commit_total_gb"]
        if "logical_cores" in stats:
            pkt["lcores"] = int(stats["logical_cores"])
        if "disk_r_mbps" in stats:
            pkt["dr"] = round(float(stats["disk_r_mbps"]), 1)
        if "disk_w_mbps" in stats:
            pkt["dw"] = round(float(stats["disk_w_mbps"]), 1)
        return pkt
