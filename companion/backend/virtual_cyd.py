from __future__ import annotations

import ctypes
import io
import json
import os
import shutil
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Callable

from PIL import Image

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
VCYD_DIR = Path(__file__).resolve().parent.parent / "virtual_cyd"
DLL_PATH = VCYD_DIR / "hydro_virtual_cyd.dll"
STORAGE_DIR = Path(__file__).resolve().parent.parent / "data" / "virtual_cyd"

SCREEN_W = 320
SCREEN_H = 240
FB_BYTES = SCREEN_W * SCREEN_H * 4


class VirtualCydBridge:
    """Hosts the natively compiled C++ CYD firmware DLL and emulates SPI/TFT/Touch/LED/Audio in real time."""

    def __init__(self, on_event: Callable[[dict[str, Any]], None] | None = None, storage_dir: Path | None = None):
        self.on_event = on_event
        self.storage_dir = Path(storage_dir or STORAGE_DIR)
        self.storage_dir.mkdir(parents=True, exist_ok=True)

        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._lib: ctypes.CDLL | None = None
        self._loaded_copy: Path | None = None

        self._fb_buf = (ctypes.c_uint8 * FB_BYTES)()
        self._serial_buf = ctypes.create_string_buffer(32768)
        self._state_buf = ctypes.create_string_buffer(8192)
        self._audio_buf = ctypes.create_string_buffer(8192)
        self._rx_accum = ""

        self.initialized = False
        self.hydro_handshake = False
        self.panel_type = "ili9341"
        self.last_state: dict[str, Any] = {}
        self.last_error: str = ""
        self.serial_logs: list[dict[str, str]] = []
        self.audio_queue: list[dict[str, Any]] = []
        self._on_frame_cb: Callable[[], None] | None = None
        self._frame_seq: int = 0
        self._last_rgba = bytes(FB_BYTES)

    def _log_serial(self, direction: str, text: str) -> None:
        line = text.strip()
        if not line:
            return
        entry = {
            "ts": time.strftime("%H:%M:%S"),
            "dir": direction,
            "line": line[:320],
        }
        self.serial_logs.insert(0, entry)
        del self.serial_logs[80:]

    def _bind_dll(self) -> bool:
        if not DLL_PATH.exists():
            self.last_error = f"Virtual CYD DLL missing at {DLL_PATH}"
            return False
        # Copy DLL to a per-load temp name so build_vcyd.bat is never blocked by Windows file locking
        tmp_dir = Path(tempfile.gettempdir()) / "hydro_vcyd"
        tmp_dir.mkdir(parents=True, exist_ok=True)
        cutoff = time.time() - 60.0
        for old_dll in tmp_dir.glob("hydro_virtual_cyd_*.dll"):
            try:
                if old_dll.stat().st_mtime < cutoff:
                    old_dll.unlink(missing_ok=True)
            except OSError:
                pass
        copy_path = tmp_dir / f"hydro_virtual_cyd_{os.getpid()}_{int(time.time() * 1000)}.dll"
        shutil.copyfile(DLL_PATH, copy_path)
        self._loaded_copy = copy_path

        lib = ctypes.CDLL(str(copy_path))
        lib.vcyd_init.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vcyd_init.restype = ctypes.c_int

        lib.vcyd_shutdown.argtypes = []
        lib.vcyd_shutdown.restype = None

        lib.vcyd_set_panel_type.argtypes = [ctypes.c_int]
        lib.vcyd_set_panel_type.restype = None

        lib.vcyd_tick.argtypes = []
        lib.vcyd_tick.restype = None

        lib.vcyd_feed_serial.argtypes = [ctypes.c_char_p]
        lib.vcyd_feed_serial.restype = None

        lib.vcyd_pop_serial.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vcyd_pop_serial.restype = ctypes.c_int

        lib.vcyd_set_touch_screen_px.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.vcyd_set_touch_screen_px.restype = None

        lib.vcyd_set_touch_raw.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.vcyd_set_touch_raw.restype = None

        lib.vcyd_set_boot_btn.argtypes = [ctypes.c_int]
        lib.vcyd_set_boot_btn.restype = None

        lib.vcyd_get_framebuffer_rgba.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
        lib.vcyd_get_framebuffer_rgba.restype = ctypes.c_int

        lib.vcyd_get_state_json.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vcyd_get_state_json.restype = ctypes.c_int

        lib.vcyd_pop_audio_json.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.vcyd_pop_audio_json.restype = ctypes.c_int

        self._lib = lib
        return True

    def start(self, panel_type: str = "ili9341", start_loop: bool = True) -> bool:
        with self._lock:
            self.stop()
            self.panel_type = "st7789" if str(panel_type).lower() == "st7789" else "ili9341"
            if self._lib is None and not self._bind_dll():
                return False
            assert self._lib is not None
            panel_id = 1 if self.panel_type == "st7789" else 0
            ok = self._lib.vcyd_init(str(self.storage_dir).encode("utf-8"), panel_id)
            self.initialized = bool(ok)
            self.hydro_handshake = False
            self.last_error = ""
            self._pull_outputs_locked()
            if start_loop:
                self._stop.clear()
                self._thread = threading.Thread(target=self._run_loop, name="hydro_virtual_cyd", daemon=True)
                self._thread.start()
            return self.initialized

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.5)
        self._thread = None
        with self._lock:
            if self._lib is not None and self.initialized:
                try:
                    self._lib.vcyd_shutdown()
                except Exception:
                    pass
                self.initialized = False

    def reset(self, clear_nvs: bool = False) -> bool:
        with self._lock:
            panel = self.panel_type
            self.stop()
            if clear_nvs and self.storage_dir.exists():
                shutil.rmtree(self.storage_dir, ignore_errors=True)
                self.storage_dir.mkdir(parents=True, exist_ok=True)
            return self.start(panel_type=panel, start_loop=True)

    def set_panel(self, panel_type: str) -> None:
        with self._lock:
            self.panel_type = "st7789" if str(panel_type).lower() == "st7789" else "ili9341"
            if self._lib and self.initialized:
                self._lib.vcyd_set_panel_type(1 if self.panel_type == "st7789" else 0)
                self._pull_outputs_locked()

    def set_frame_callback(self, cb: Callable[[], None] | None) -> None:
        self._on_frame_cb = cb

    def tick_once(self) -> None:
        events: list[dict[str, Any]] = []
        with self._lock:
            if not self._lib or not self.initialized:
                return
            self._lib.vcyd_tick()
            events = self._pull_outputs_locked()
        for ev in events:
            if self.on_event:
                try:
                    self.on_event(ev)
                except Exception as exc:
                    self.last_error = f"event hook err: {exc}"

    def _run_loop(self) -> None:
        while not self._stop.is_set():
            t0 = time.perf_counter()
            try:
                self.tick_once()
            except Exception as exc:
                self.last_error = f"vcyd tick error: {exc}"
            elapsed = time.perf_counter() - t0
            rem = 0.032 - elapsed  # ~31 Hz responsive UI + timer cadence
            if rem > 0.001:
                self._stop.wait(rem)

    def _pull_outputs_locked(self) -> list[dict[str, Any]]:
        if not self._lib:
            return []
        events: list[dict[str, Any]] = []

        # 1. Serial TX lines from Virtual CYD -> Companion
        n_ser = self._lib.vcyd_pop_serial(self._serial_buf, ctypes.sizeof(self._serial_buf))
        if n_ser > 0:
            chunk = self._serial_buf.raw[:n_ser].decode("utf-8", errors="ignore")
            self._rx_accum += chunk
            while "\n" in self._rx_accum:
                line, self._rx_accum = self._rx_accum.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                self._log_serial("CYD->PC", line)
                if line.startswith("{"):
                    try:
                        pkt = json.loads(line)
                        if isinstance(pkt, dict):
                            if pkt.get("t") == "ready":
                                self.hydro_handshake = True
                            events.append(pkt)
                    except json.JSONDecodeError:
                        pass

        # 2. Framebuffer RGBA
        n_fb = self._lib.vcyd_get_framebuffer_rgba(self._fb_buf, FB_BYTES)
        if n_fb == FB_BYTES:
            raw = bytes(self._fb_buf)
            if raw != self._last_rgba:
                self._last_rgba = raw
                self._frame_seq += 1
                if self._on_frame_cb:
                    try:
                        self._on_frame_cb()
                    except Exception:
                        pass

        # 3. State JSON
        n_st = self._lib.vcyd_get_state_json(self._state_buf, ctypes.sizeof(self._state_buf))
        if n_st > 0:
            try:
                self.last_state = json.loads(self._state_buf.raw[:n_st].decode("utf-8", errors="ignore"))
            except Exception:
                pass

        # 4. Audio SFX events
        n_au = self._lib.vcyd_pop_audio_json(self._audio_buf, ctypes.sizeof(self._audio_buf))
        if n_au > 0:
            try:
                au = json.loads(self._audio_buf.raw[:n_au].decode("utf-8", errors="ignore"))
                if isinstance(au, list) and au:
                    self.audio_queue.extend(au)
                    del self.audio_queue[:-32]
            except Exception:
                pass

        return events

    def send_packet(self, payload: dict[str, Any]) -> bool:
        with self._lock:
            if not self._lib or not self.initialized:
                return False
            line = json.dumps(payload, separators=(",", ":")) + "\n"
            self._lib.vcyd_feed_serial(line.encode("utf-8"))
            self._log_serial("PC->CYD", line)
            return True

    def set_touch(self, down: bool, x: int = 0, y: int = 0) -> None:
        with self._lock:
            if not self._lib or not self.initialized:
                return
            self._lib.vcyd_set_touch_screen_px(1 if down else 0, int(x), int(y))

    def set_touch_raw(self, down: bool, raw_x: int, raw_y: int) -> None:
        with self._lock:
            if not self._lib or not self.initialized:
                return
            self._lib.vcyd_set_touch_raw(1 if down else 0, int(raw_x), int(raw_y))

    def set_boot(self, down: bool) -> None:
        with self._lock:
            if not self._lib or not self.initialized:
                return
            self._lib.vcyd_set_boot_btn(1 if down else 0)

    def tap_px(self, x: int, y: int, hold_ms: int = 75) -> None:
        """Deterministic helper (used in tests and quick scripts) to tap exact screen pixels through real XPT2046 SPI."""
        self.set_touch(True, x, y)
        end = time.perf_counter() + (hold_ms / 1000.0)
        while time.perf_counter() < end:
            self.tick_once()
            time.sleep(0.016)
        self.set_touch(False, x, y)
        for _ in range(3):
            self.tick_once()
            time.sleep(0.016)

    def hold_px(self, x: int, y: int, hold_ms: int = 620) -> None:
        self.tap_px(x, y, hold_ms=hold_ms)

    def drag_px(self, x0: int, y0: int, x1: int, y1: int, steps: int = 10, step_ms: int = 20) -> None:
        for s in range(steps + 1):
            t = s / max(1, steps)
            x = int(round(x0 + (x1 - x0) * t))
            y = int(round(y0 + (y1 - y0) * t))
            self.set_touch(True, x, y)
            self.tick_once()
            time.sleep(step_ms / 1000.0)
        self.set_touch(False, x1, y1)
        for _ in range(3):
            self.tick_once()
            time.sleep(0.016)

    def get_rgba_bytes(self) -> bytes:
        with self._lock:
            return self._last_rgba

    def render_png_bytes(self, scale: int = 1) -> bytes:
        rgba = self.get_rgba_bytes()
        img = Image.frombytes("RGBA", (SCREEN_W, SCREEN_H), rgba)
        if scale > 1:
            img = img.resize((SCREEN_W * scale, SCREEN_H * scale), Image.Resampling.NEAREST)
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        return buf.getvalue()

    def pop_audio_events(self) -> list[dict[str, Any]]:
        with self._lock:
            evs = list(self.audio_queue)
            self.audio_queue.clear()
            return evs

    def get_state(self) -> dict[str, Any]:
        with self._lock:
            st = dict(self.last_state)
            st["frame_seq"] = self._frame_seq
            st["hydro_handshake"] = self.hydro_handshake
            st["last_error"] = self.last_error
            st["serial_logs"] = list(self.serial_logs[:40])
            return st

