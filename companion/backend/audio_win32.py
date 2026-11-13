from __future__ import annotations

import ctypes
import logging
from ctypes import POINTER, byref, c_float, c_uint, c_void_p, wintypes
from typing import Any
import win32gui
import win32process
import psutil

logger = logging.getLogger(__name__)


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_ulong),
        ("Data2", ctypes.c_ushort),
        ("Data3", ctypes.c_ushort),
        ("Data4", ctypes.c_ubyte * 8),
    ]

    @classmethod
    def from_str(cls, s: str) -> "GUID":
        s = s.strip("{}")
        parts = s.split("-")
        d1 = int(parts[0], 16)
        d2 = int(parts[1], 16)
        d3 = int(parts[2], 16)
        tail = bytes.fromhex(parts[3] + parts[4])
        arr = (ctypes.c_ubyte * 8)(*tail)
        return cls(d1, d2, d3, arr)


CLSID_MMDeviceEnumerator = GUID.from_str("{BCDE0395-E52F-467C-8E3D-C4579291692E}")
IID_IMMDeviceEnumerator = GUID.from_str("{A95664D2-9614-4F35-A746-DE8DB63617E6}")
IID_IAudioEndpointVolume = GUID.from_str("{5CDF2C82-841E-4546-9722-0CF74078229A}")
CLSID_PolicyConfigClient = GUID.from_str("{870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}")
IID_IPolicyConfig = GUID.from_str("{F8679F50-850A-41CF-9C72-430F290290C8}")
CLSID_PolicyConfigVista = GUID.from_str("{294935CE-F637-4E7C-A41B-AB255460B862}")
IID_IPolicyConfigVista = GUID.from_str("{568B9108-44BF-40B4-9006-86AFE5B5A620}")


class PROPERTYKEY(ctypes.Structure):
    _fields_ = [("fmtid", GUID), ("pid", wintypes.DWORD)]


class PROPVARIANT(ctypes.Structure):
    _fields_ = [
        ("vt", ctypes.c_ushort),
        ("wReserved1", ctypes.c_ushort),
        ("wReserved2", ctypes.c_ushort),
        ("wReserved3", ctypes.c_ushort),
        ("pwszVal", c_void_p),
        ("pad", ctypes.c_ulonglong),
    ]


PKEY_Device_FriendlyName = PROPERTYKEY(
    GUID.from_str("{A45C254E-DF1C-4EFD-8020-67D146A850E0}"), 14
)

E_RENDER = 0
E_CAPTURE = 1
E_MULTIMEDIA = 1
DEVICE_STATE_ACTIVE = 1
CLSCTX_ALL = 0x17

ole32 = ctypes.windll.ole32
ole32.CoTaskMemFree.argtypes = [c_void_p]
ole32.CoTaskMemFree.restype = None
ole32.PropVariantClear.argtypes = [c_void_p]
ole32.PropVariantClear.restype = ctypes.c_long


def _vtable_call(ptr: c_void_p, index: int, restype: Any, argtypes: list[Any], *args: Any) -> Any:
    vtbl_ptr = ctypes.cast(ptr, POINTER(POINTER(c_void_p))).contents
    fn_addr = vtbl_ptr[index]
    fn_type = ctypes.WINFUNCTYPE(restype, c_void_p, *argtypes)
    return fn_type(fn_addr)(ptr, *args)


def _with_endpoint(data_flow: int, fn):
    # Initialize COM MTA, falling back gracefully if thread already uses STA (RPC_E_CHANGED_MODE = 0x80010106)
    hr_init = ole32.CoInitializeEx(None, 0x0)
    should_uninit = hr_init in (0, 1)
    if hr_init not in (0, 1, -2147417850):
        logger.debug("CoInitializeEx failed: hr=%#x", hr_init & 0xFFFFFFFF)
        return None
    enum_ptr = c_void_p()
    dev_ptr = c_void_p()
    vol_ptr = c_void_p()
    try:
        hr = ole32.CoCreateInstance(
            byref(CLSID_MMDeviceEnumerator),
            None,
            CLSCTX_ALL,
            byref(IID_IMMDeviceEnumerator),
            byref(enum_ptr),
        )
        if hr != 0 or not enum_ptr.value:
            logger.debug("CoCreateInstance IMMDeviceEnumerator failed: hr=%#x", hr & 0xFFFFFFFF)
            return None
        # IMMDeviceEnumerator::GetDefaultAudioEndpoint is vtable index 4
        hr = _vtable_call(enum_ptr, 4, ctypes.c_long, [c_uint, c_uint, POINTER(c_void_p)],
                          data_flow, E_MULTIMEDIA, byref(dev_ptr))
        if hr != 0 or not dev_ptr.value:
            logger.debug("GetDefaultAudioEndpoint(%d) failed: hr=%#x", data_flow, hr & 0xFFFFFFFF)
            return None
        # IMMDevice::Activate is vtable index 3
        hr = _vtable_call(dev_ptr, 3, ctypes.c_long,
                          [POINTER(GUID), c_uint, c_void_p, POINTER(c_void_p)],
                          byref(IID_IAudioEndpointVolume), CLSCTX_ALL, None, byref(vol_ptr))
        if hr != 0 or not vol_ptr.value:
            logger.debug("IMMDevice::Activate failed: hr=%#x", hr & 0xFFFFFFFF)
            return None
        return fn(vol_ptr)
    except Exception as exc:
        logger.debug("COM endpoint call failed: %s", exc)
        return None
    finally:
        for p in (vol_ptr, dev_ptr, enum_ptr):
            if p and p.value:
                try:
                    _vtable_call(p, 2, c_uint, [])  # IUnknown::Release
                except Exception as exc:
                    logger.debug("IUnknown::Release failed: %s", exc)
        if should_uninit:
            try:
                ole32.CoUninitialize()
            except Exception as exc:
                logger.debug("CoUninitialize failed: %s", exc)


def get_master_volume() -> tuple[int, bool]:
    def _read(ep: c_void_p):
        lvl = c_float(0.0)
        mute = wintypes.BOOL(False)
        # GetMasterVolumeLevelScalar idx 9, GetMute idx 15
        hr1 = _vtable_call(ep, 9, ctypes.c_long, [POINTER(c_float)], byref(lvl))
        hr2 = _vtable_call(ep, 15, ctypes.c_long, [POINTER(wintypes.BOOL)], byref(mute))
        if hr1 < 0 or hr2 < 0:
            return None
        return int(round(max(0.0, min(1.0, lvl.value)) * 100)), bool(mute.value)

    res = _with_endpoint(E_RENDER, _read)
    return res if res is not None else (50, False)


def set_master_volume(pct: int) -> int:
    pct = max(0, min(100, int(pct)))
    scalar = pct / 100.0

    def _write(ep: c_void_p):
        # SetMasterVolumeLevelScalar idx 7
        hr = _vtable_call(ep, 7, ctypes.c_long, [c_float, c_void_p], c_float(scalar), None)
        return None if hr < 0 else pct

    res = _with_endpoint(E_RENDER, _write)
    return res if res is not None else pct


def toggle_master_mute() -> bool:
    def _tog(ep: c_void_p):
        mute = wintypes.BOOL(False)
        hr1 = _vtable_call(ep, 15, ctypes.c_long, [POINTER(wintypes.BOOL)], byref(mute))
        if hr1 < 0:
            return None
        new_val = not bool(mute.value)
        # SetMute idx 14
        hr2 = _vtable_call(ep, 14, ctypes.c_long, [wintypes.BOOL, c_void_p], wintypes.BOOL(new_val), None)
        return None if hr2 < 0 else new_val

    res = _with_endpoint(E_RENDER, _tog)
    return bool(res) if res is not None else False


def get_mic_muted() -> bool:
    def _read(ep: c_void_p):
        mute = wintypes.BOOL(False)
        hr = _vtable_call(ep, 15, ctypes.c_long, [POINTER(wintypes.BOOL)], byref(mute))
        return None if hr < 0 else bool(mute.value)

    res = _with_endpoint(E_CAPTURE, _read)
    return bool(res) if res is not None else False


def toggle_mic_mute() -> bool:
    def _tog(ep: c_void_p):
        mute = wintypes.BOOL(False)
        hr1 = _vtable_call(ep, 15, ctypes.c_long, [POINTER(wintypes.BOOL)], byref(mute))
        if hr1 < 0:
            return None
        new_val = not bool(mute.value)
        hr2 = _vtable_call(ep, 14, ctypes.c_long, [wintypes.BOOL, c_void_p], wintypes.BOOL(new_val), None)
        return None if hr2 < 0 else new_val

    res = _with_endpoint(E_CAPTURE, _tog)
    return bool(res) if res is not None else False


def detect_media_title() -> tuple[bool, str]:
    """Reads playing status / track title from open media player window titles (no network mocking)."""
    players = {
        "spotify.exe": "Spotify",
        "vlc.exe": "VLC",
        "musicbee.exe": "MusicBee",
        "foobar2000.exe": "foobar2000",
    }
    found: list[tuple[bool, str]] = []

    def _cb(hwnd, _):
        if not win32gui.IsWindowVisible(hwnd):
            return True
        title = win32gui.GetWindowText(hwnd) or ""
        if not title:
            return True
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            pname = psutil.Process(pid).name().lower()
            if pname in players:
                if pname == "spotify.exe":
                    if title in ("Spotify", "Spotify Free", "Spotify Premium"):
                        found.append((False, "Spotify Paused"))
                    else:
                        found.append((True, title[:40]))
                else:
                    found.append((True, title[:40]))
        except Exception:
            pass
        return True

    try:
        win32gui.EnumWindows(_cb, None)
    except Exception:
        pass

    for playing, txt in found:
        if playing:
            return True, txt
    if found:
        return found[0]
    return False, "PC Audio Ready"


def list_audio_devices() -> list[dict[str, Any]]:
    """Enumerates active Windows CoreAudio render (playback) & capture (mic) endpoints."""
    hr_init = ole32.CoInitializeEx(None, 0x0)
    should_uninit = hr_init in (0, 1)
    if hr_init not in (0, 1, -2147417850):
        return []
    devices: list[dict[str, Any]] = []
    enum_ptr = c_void_p()
    try:
        hr = ole32.CoCreateInstance(
            byref(CLSID_MMDeviceEnumerator),
            None,
            CLSCTX_ALL,
            byref(IID_IMMDeviceEnumerator),
            byref(enum_ptr),
        )
        if hr != 0 or not enum_ptr.value:
            return []

        for flow_idx, flow_name in ((E_RENDER, "render"), (E_CAPTURE, "capture")):
            default_id = ""
            def_dev = c_void_p()
            hr_def = _vtable_call(
                enum_ptr, 4, ctypes.c_long, [c_uint, c_uint, POINTER(c_void_p)],
                flow_idx, E_MULTIMEDIA, byref(def_dev),
            )
            if hr_def == 0 and def_dev.value:
                id_p = c_void_p()
                if _vtable_call(def_dev, 5, ctypes.c_long, [POINTER(c_void_p)], byref(id_p)) == 0 and id_p.value:
                    default_id = ctypes.wstring_at(id_p.value)
                    ole32.CoTaskMemFree(id_p)
                _vtable_call(def_dev, 2, c_uint, [])

            col_ptr = c_void_p()
            # IMMDeviceEnumerator::EnumAudioEndpoints (vtable index 3)
            hr_col = _vtable_call(
                enum_ptr, 3, ctypes.c_long, [c_uint, c_uint, POINTER(c_void_p)],
                flow_idx, DEVICE_STATE_ACTIVE, byref(col_ptr),
            )
            if hr_col != 0 or not col_ptr.value:
                continue
            try:
                cnt = c_uint(0)
                if _vtable_call(col_ptr, 3, ctypes.c_long, [POINTER(c_uint)], byref(cnt)) != 0:
                    continue
                for idx in range(cnt.value):
                    dev_p = c_void_p()
                    if _vtable_call(col_ptr, 4, ctypes.c_long, [c_uint, POINTER(c_void_p)], idx, byref(dev_p)) != 0 or not dev_p.value:
                        continue
                    try:
                        id_p = c_void_p()
                        dev_id = ""
                        if _vtable_call(dev_p, 5, ctypes.c_long, [POINTER(c_void_p)], byref(id_p)) == 0 and id_p.value:
                            dev_id = ctypes.wstring_at(id_p.value)
                            ole32.CoTaskMemFree(id_p)
                        prop_p = c_void_p()
                        friendly = dev_id
                        # IMMDevice::OpenPropertyStore (vtable index 4)
                        if _vtable_call(dev_p, 4, ctypes.c_long, [c_uint, POINTER(c_void_p)], 0, byref(prop_p)) == 0 and prop_p.value:
                            try:
                                pv = PROPVARIANT()
                                # IPropertyStore::GetValue (vtable index 5)
                                if _vtable_call(prop_p, 5, ctypes.c_long, [POINTER(PROPERTYKEY), POINTER(PROPVARIANT)], byref(PKEY_Device_FriendlyName), byref(pv)) == 0:
                                    if pv.vt == 31 and pv.pwszVal:
                                        friendly = ctypes.wstring_at(pv.pwszVal)
                                    ole32.PropVariantClear(byref(pv))
                            finally:
                                _vtable_call(prop_p, 2, c_uint, [])
                        if dev_id:
                            devices.append({
                                "id": dev_id,
                                "name": friendly,
                                "flow": flow_name,
                                "default": dev_id == default_id,
                            })
                    finally:
                        _vtable_call(dev_p, 2, c_uint, [])
            finally:
                _vtable_call(col_ptr, 2, c_uint, [])
        return devices
    except Exception as exc:
        logger.debug("list_audio_devices failed: %s", exc)
        return []
    finally:
        if enum_ptr.value:
            try:
                _vtable_call(enum_ptr, 2, c_uint, [])
            except Exception:
                pass
        if should_uninit:
            try:
                ole32.CoUninitialize()
            except Exception:
                pass


def set_default_audio_device(query_or_id: str, flow: str = "") -> dict[str, Any]:
    """Switches Windows default audio endpoint across Console/Multimedia/Comms via IPolicyConfig."""
    q = (query_or_id or "").strip()
    if not q:
        raise ValueError("audio_device requires a device name or ID")

    devices = list_audio_devices()
    candidates = [d for d in devices if not flow or d["flow"] == flow]
    q_low = q.lower()
    match = next((d for d in candidates if d["id"].lower() == q_low), None)
    if match is None:
        match = next((d for d in candidates if d["name"].lower() == q_low), None)
    if match is None:
        match = next((d for d in candidates if q_low in d["name"].lower()), None)
    if match is None:
        raise ValueError(f"Audio endpoint not found: {q}")

    hr_init = ole32.CoInitializeEx(None, 0x0)
    should_uninit = hr_init in (0, 1)
    if hr_init not in (0, 1, -2147417850):
        raise RuntimeError(f"COM init failed ({hr_init & 0xFFFFFFFF:#x})")

    pc_ptr = c_void_p()
    try:
        hr = ole32.CoCreateInstance(
            byref(CLSID_PolicyConfigClient),
            None,
            CLSCTX_ALL,
            byref(IID_IPolicyConfig),
            byref(pc_ptr),
        )
        if hr != 0 or not pc_ptr.value:
            hr = ole32.CoCreateInstance(
                byref(CLSID_PolicyConfigVista),
                None,
                CLSCTX_ALL,
                byref(IID_IPolicyConfigVista),
                byref(pc_ptr),
            )
        if hr != 0 or not pc_ptr.value:
            raise RuntimeError(f"PolicyConfig COM unavailable (hr={hr & 0xFFFFFFFF:#x})")

        # IPolicyConfig::SetDefaultEndpoint (vtable index 13) for Console(0), Multimedia(1), Comms(2)
        for role in (0, 1, 2):
            hr_set = _vtable_call(
                pc_ptr, 13, ctypes.c_long, [ctypes.c_wchar_p, c_uint],
                match["id"], role,
            )
            if hr_set != 0:
                raise RuntimeError(f"SetDefaultEndpoint failed (hr={hr_set & 0xFFFFFFFF:#x})")
        return match
    finally:
        if pc_ptr.value:
            try:
                _vtable_call(pc_ptr, 2, c_uint, [])
            except Exception:
                pass
        if should_uninit:
            try:
                ole32.CoUninitialize()
            except Exception:
                pass

