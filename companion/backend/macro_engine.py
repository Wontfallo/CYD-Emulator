from __future__ import annotations

import ctypes
import datetime
import ipaddress
import json
import os
import shlex
import socket
import subprocess
import threading
import time
import urllib.parse
import urllib.request
import webbrowser
import winsound
from ctypes import wintypes
from pathlib import Path
from typing import Any

import psutil
import win32con
import win32gui
import win32process
from PIL import ImageGrab

from .audio_win32 import (
    get_master_volume,
    get_mic_muted,
    list_audio_devices,
    set_default_audio_device,
    set_master_volume,
    toggle_master_mute,
    toggle_mic_mute,
)

# --- Win32 SendInput Structures ---
INPUT_KEYBOARD = 1
KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_UNICODE = 0x0004

ULONG_PTR = ctypes.c_ulonglong if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_ulong


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", wintypes.DWORD),
        ("wParamL", wintypes.WORD),
        ("wParamH", wintypes.WORD),
    ]


class _INPUT_UNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("_u",)
    _fields_ = [("type", wintypes.DWORD), ("_u", _INPUT_UNION)]


user32 = ctypes.windll.user32

VK_MAP: dict[str, int] = {
    "CTRL": 0xA2,
    "LCTRL": 0xA2,
    "RCTRL": 0xA3,
    "SHIFT": 0xA0,
    "LSHIFT": 0xA0,
    "RSHIFT": 0xA1,
    "ALT": 0xA4,
    "LALT": 0xA4,
    "RALT": 0xA5,
    "WIN": 0x5B,
    "LWIN": 0x5B,
    "RWIN": 0x5C,
    "ENTER": 0x0D,
    "RETURN": 0x0D,
    "TAB": 0x09,
    "ESC": 0x1B,
    "ESCAPE": 0x1B,
    "SPACE": 0x20,
    "BACKSPACE": 0x08,
    "BKSP": 0x08,
    "DEL": 0x2E,
    "DELETE": 0x2E,
    "INS": 0x2D,
    "INSERT": 0x2D,
    "HOME": 0x24,
    "END": 0x23,
    "PGUP": 0x21,
    "PAGEUP": 0x21,
    "PGDN": 0x22,
    "PAGEDOWN": 0x22,
    "UP": 0x26,
    "DOWN": 0x28,
    "LEFT": 0x25,
    "RIGHT": 0x27,
    "PRTSC": 0x2C,
    "PRINTSCREEN": 0x2C,
    "SPACEBAR": 0x20,
    "CAPS": 0x14,
    "PLAY_PAUSE": 0xB3,
    "MEDIA_NEXT": 0xB0,
    "MEDIA_PREV": 0xB1,
    "MEDIA_STOP": 0xB2,
    "VOL_MUTE": 0xAD,
    "VOL_DOWN": 0xAE,
    "VOL_UP": 0xAF,
}
for _i in range(1, 25):
    VK_MAP[f"F{_i}"] = 0x70 + (_i - 1)
for _n in range(0, 10):
    VK_MAP[f"NUM{_n}"] = 0x60 + _n
for _c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
    VK_MAP[_c] = ord(_c)
VK_MAP.update({
    "-": 0xBD, "MINUS": 0xBD, "=": 0xBB, "EQUAL": 0xBB, "+": 0xBB, "PLUS": 0xBB,
    "[": 0xDB, "]": 0xDD, "\\": 0xDC,
    ";": 0xBA, "'": 0xDE, ",": 0xBC, ".": 0xBE, "/": 0xBF, "`": 0xC0,
})

EXTENDED_VKS = {0x5B, 0x5C, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0xB0, 0xB1, 0xB2, 0xB3, 0xAD, 0xAE, 0xAF}

CF_UNICODETEXT = 13


def parse_hotkey(combo: str) -> list[int]:
    raw = (combo or "").strip()
    if raw.endswith("++"):
        raw = raw[:-1] + "PLUS"
    if raw.endswith("--") and "+" not in raw:
        raw = raw[:-1] + "MINUS"
    if "+" in raw or "-" not in raw or raw == "-":
        tokens = [t.strip().upper() for t in raw.split("+") if t.strip()]
    else:
        tokens = [t.strip().upper() for t in raw.split("-") if t.strip()]
    vks = []
    for tok in tokens:
        if tok not in VK_MAP:
            raise ValueError(f"Unknown key token in hotkey: {tok!r}")
        vks.append(VK_MAP[tok])
    if not vks:
        raise ValueError("Empty hotkey string")
    return vks


def _send_vk_inputs(vks: list[int]) -> int:
    events: list[INPUT] = []
    for vk in vks:
        flags = KEYEVENTF_EXTENDEDKEY if vk in EXTENDED_VKS else 0
        events.append(INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(wVk=vk, wScan=0, dwFlags=flags, time=0, dwExtraInfo=0)))
    for vk in reversed(vks):
        flags = KEYEVENTF_KEYUP | (KEYEVENTF_EXTENDEDKEY if vk in EXTENDED_VKS else 0)
        events.append(INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(wVk=vk, wScan=0, dwFlags=flags, time=0, dwExtraInfo=0)))
    if not events:
        return 0
    arr = (INPUT * len(events))(*events)
    sent = int(user32.SendInput(len(arr), arr, ctypes.sizeof(INPUT)))
    if sent != len(events):
        raise RuntimeError(f"SendInput injected {sent}/{len(events)} key events (blocked or UIPI protected)")
    return sent


def _send_unicode_text(text: str) -> int:
    events: list[INPUT] = []
    for ch in text:
        if ch == "\n":
            events.append(INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(wVk=0x0D, wScan=0, dwFlags=0, time=0, dwExtraInfo=0)))
            events.append(INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(wVk=0x0D, wScan=0, dwFlags=KEYEVENTF_KEYUP, time=0, dwExtraInfo=0)))
            continue
        code = ord(ch)
        events.append(INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(wVk=0, wScan=code, dwFlags=KEYEVENTF_UNICODE, time=0, dwExtraInfo=0)))
        events.append(INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(wVk=0, wScan=code, dwFlags=KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, time=0, dwExtraInfo=0)))
    if not events:
        return 0
    arr = (INPUT * len(events))(*events)
    sent = int(user32.SendInput(len(arr), arr, ctypes.sizeof(INPUT)))
    if sent != len(events):
        raise RuntimeError(f"SendInput injected {sent}/{len(events)} unicode events")
    return sent


kernel32 = ctypes.windll.kernel32
winmm = ctypes.windll.winmm
winmm.mciSendStringW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.UINT, wintypes.HANDLE]
winmm.mciSendStringW.restype = wintypes.UINT
user32.OpenClipboard.argtypes = [wintypes.HWND]
user32.OpenClipboard.restype = wintypes.BOOL
user32.GetClipboardData.argtypes = [wintypes.UINT]
user32.GetClipboardData.restype = wintypes.HANDLE
user32.EmptyClipboard.argtypes = []
user32.EmptyClipboard.restype = wintypes.BOOL
user32.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]
user32.SetClipboardData.restype = wintypes.HANDLE
user32.CloseClipboard.argtypes = []
user32.CloseClipboard.restype = wintypes.BOOL
kernel32.GlobalAlloc.argtypes = [wintypes.UINT, ctypes.c_size_t]
kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
kernel32.GlobalFree.argtypes = [wintypes.HGLOBAL]
kernel32.GlobalFree.restype = wintypes.HGLOBAL
kernel32.GlobalLock.argtypes = [wintypes.HANDLE]
kernel32.GlobalLock.restype = ctypes.c_void_p
kernel32.GlobalUnlock.argtypes = [wintypes.HANDLE]
kernel32.GlobalUnlock.restype = wintypes.BOOL


def _read_clipboard_text() -> str:
    if not user32.OpenClipboard(None):
        return ""
    try:
        h = user32.GetClipboardData(CF_UNICODETEXT)
        if not h:
            return ""
        ptr = kernel32.GlobalLock(h)
        if not ptr:
            return ""
        try:
            return ctypes.wstring_at(ptr) or ""
        finally:
            kernel32.GlobalUnlock(h)
    except Exception:
        return ""
    finally:
        user32.CloseClipboard()


def _write_clipboard_text(text: str) -> bool:
    raw = (text or "").encode("utf-16-le") + b"\x00\x00"
    GMEM_MOVEABLE = 0x0002
    GMEM_ZEROINIT = 0x0040
    hmem = kernel32.GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, len(raw))
    if not hmem:
        return False
    ptr = kernel32.GlobalLock(hmem)
    if not ptr:
        kernel32.GlobalFree(hmem)
        return False
    try:
        ctypes.memmove(ptr, raw, len(raw))
    finally:
        kernel32.GlobalUnlock(hmem)
    if not user32.OpenClipboard(None):
        kernel32.GlobalFree(hmem)
        return False
    try:
        user32.EmptyClipboard()
        if not user32.SetClipboardData(CF_UNICODETEXT, hmem):
            kernel32.GlobalFree(hmem)
            return False
        return True
    finally:
        user32.CloseClipboard()


def _mci_cmd(cmd: str) -> int:
    rc = int(winmm.mciSendStringW(cmd, None, 0, None))
    if rc != 0:
        buf = ctypes.create_unicode_buffer(256)
        try:
            winmm.mciGetErrorStringW(rc, buf, len(buf))
            msg = buf.value.strip() or "Unknown MCI error"
        except Exception:
            msg = "Unknown MCI error"
        raise ValueError(f"MCI audio error ({rc}): {msg}")
    return rc


def _is_blocked_ip(ip_obj: ipaddress._BaseAddress) -> bool:
    return bool(
        ip_obj.is_private
        or ip_obj.is_loopback
        or ip_obj.is_link_local
        or ip_obj.is_multicast
        or ip_obj.is_reserved
        or ip_obj.is_unspecified
    )


def _validate_webhook_url(url: str, allow_private: bool = False) -> str:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        raise ValueError("Webhook URL must be a valid http:// or https:// URL")
    if allow_private:
        return url
    host = parsed.hostname.strip().lower().rstrip(".")
    if host in ("localhost", "localhost.localdomain") or host.endswith((".localhost", ".local")):
        raise ValueError(f"Webhook blocked private/local host '{host}' (set allow_private=true to override)")
    try:
        ip_literal = ipaddress.ip_address(host)
    except ValueError:
        ip_literal = None
    if ip_literal is not None:
        if _is_blocked_ip(ip_literal):
            raise ValueError(f"Webhook blocked private/reserved IP '{host}' (set allow_private=true to override)")
        return url
    try:
        infos = socket.getaddrinfo(host, None)
        for info in infos:
            addr_raw = info[4][0].split("%", 1)[0]
            try:
                resolved_ip = ipaddress.ip_address(addr_raw)
            except ValueError:
                continue
            if _is_blocked_ip(resolved_ip):
                raise ValueError(
                    f"Webhook blocked private/reserved target '{host}' -> {addr_raw} (set allow_private=true to override)"
                )
    except socket.gaierror:
        # Defer network DNS failures to HTTP request execution (so offline dry-run checks still work)
        pass
    return url


def expand_macro_template(tpl: str, clipboard_override: str | None = None) -> str:
    now = datetime.datetime.now()
    clip_val = clipboard_override if clipboard_override is not None else (
        _read_clipboard_text() if "{{clipboard}}" in tpl else ""
    )
    return (
        tpl.replace("{{date}}", now.strftime("%Y-%m-%d"))
        .replace("{{time}}", now.strftime("%H:%M:%S"))
        .replace("{{datetime}}", now.strftime("%Y-%m-%d %H:%M:%S"))
        .replace("{{clipboard}}", clip_val)
    )


def _focus_window_by_target(target: str) -> bool:
    needle = target.strip().lower()
    if not needle:
        return False
    matches: list[int] = []

    def _cb(hwnd: int, _):
        if not win32gui.IsWindowVisible(hwnd):
            return True
        title = (win32gui.GetWindowText(hwnd) or "").lower()
        pname = ""
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
            pname = psutil.Process(pid).name().lower()
        except Exception:
            pass
        if needle in pname or needle in title:
            matches.append(hwnd)
            return False
        return True

    try:
        win32gui.EnumWindows(_cb, None)
    except Exception:
        pass

    if not matches:
        return False
    hwnd = matches[0]
    try:
        if win32gui.IsIconic(hwnd):
            win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        win32gui.SetForegroundWindow(hwnd)
    except Exception:
        # Fallback restore + bring to top
        win32gui.ShowWindow(hwnd, win32con.SW_SHOW)
    return True


class MacroEngine:
    def __init__(self, screenshots_dir: Path | None = None):
        self.screenshots_dir = screenshots_dir or (Path.home() / "Pictures" / "HYDRO-DECK")
        self._lock = threading.RLock()
        self.history: list[dict[str, Any]] = []

    def _record(self, action_type: str, payload: Any, ok: bool, detail: str) -> dict[str, Any]:
        entry = {
            "ts": time.strftime("%H:%M:%S"),
            "action_type": action_type,
            "payload": payload,
            "ok": ok,
            "detail": detail,
        }
        with self._lock:
            self.history.insert(0, entry)
            del self.history[40:]
        return entry

    def recent_history(self, limit: int = 15) -> list[dict[str, Any]]:
        with self._lock:
            return list(self.history[:limit])

    def execute(
        self,
        action_type: str,
        payload: Any = "",
        dry_run: bool = False,
        async_webhook: bool = False,
    ) -> dict[str, Any]:
        action_type = (action_type or "").strip().lower()
        try:
            if not action_type or action_type in ("folder", "back"):
                return {"ok": True, "toast": "PAGE", "color": "#00F0DA", "detail": "Local navigation"}

            if action_type == "cyd_screen":
                target = str(payload or "deck").strip().lower()
                return {"ok": True, "toast": f"SCR:{target[:10].upper()}", "color": "#00F0DA", "detail": f"Switch CYD screen -> {target}"}

            if action_type == "timer":
                cmd = str(payload or "focus").strip().lower()
                return {"ok": True, "toast": f"TMR:{cmd[:10].upper()}", "color": "#FFB62E", "detail": f"CYD Timer -> {cmd}"}

            if action_type == "hotkey":
                vks = parse_hotkey(str(payload))
                if not dry_run:
                    _send_vk_inputs(vks)
                detail = f"Sent hotkey {payload}"
                self._record(action_type, payload, True, detail)
                return {"ok": True, "toast": str(payload)[:22], "color": "#00F0DA", "detail": detail}

            if action_type == "text":
                txt = expand_macro_template(str(payload))
                if not dry_run:
                    _send_unicode_text(txt)
                detail = f"Typed {len(txt)} chars"
                self._record(action_type, txt[:30], True, detail)
                return {"ok": True, "toast": "TEXT SENT", "color": "#3898FF", "detail": detail}

            if action_type == "clipboard":
                paste_mode = False
                copy_only = False
                if isinstance(payload, str) and payload.strip().startswith("{"):
                    try:
                        cfg = json.loads(payload)
                    except Exception:
                        cfg = {"text": payload}
                elif isinstance(payload, dict):
                    cfg = payload
                else:
                    cfg = {"text": str(payload or "")}
                tpl = str(cfg.get("text", cfg.get("template", "")))
                paste_mode = bool(cfg.get("paste", False))
                copy_only = bool(cfg.get("mode", "") == "copy")
                if not tpl:
                    raise ValueError("Clipboard macro requires template text")
                expanded = expand_macro_template(tpl)
                if not dry_run:
                    if paste_mode:
                        _write_clipboard_text(expanded)
                        _send_vk_inputs([VK_MAP["CTRL"], VK_MAP["V"]])
                    elif copy_only:
                        _write_clipboard_text(expanded)
                    else:
                        _send_unicode_text(expanded)
                detail = f"Clipboard {'paste' if paste_mode else ('copy' if copy_only else 'snippet')} ({len(expanded)} chars)"
                self._record(action_type, expanded[:30], True, detail)
                return {
                    "ok": True,
                    "toast": "CLIP PASTED" if paste_mode else ("COPIED" if copy_only else "SNIPPET SENT"),
                    "color": "#3898FF",
                    "detail": detail,
                    "expanded": expanded,
                    "paste": paste_mode,
                }

            if action_type == "soundboard":
                vol_pct = 85
                if isinstance(payload, dict):
                    raw_s = str(payload.get("path", payload.get("sound", ""))).strip()
                    vol_pct = max(0, min(100, int(payload.get("volume", 85))))
                else:
                    raw_s = str(payload or "").strip()
                if not raw_s:
                    raise ValueError("Soundboard requires .wav/.mp3 path, 'stop', or preset name")
                preset = raw_s.lower()
                if preset == "stop":
                    if not dry_run:
                        winsound.PlaySound(None, 0)
                        try:
                            _mci_cmd("close hydro_sfx")
                        except ValueError:
                            pass
                    detail = "Stopped soundboard playback"
                    self._record(action_type, "stop", True, detail)
                    return {"ok": True, "toast": "SFX STOPPED", "color": "#FFB62E", "detail": detail}
                if preset.startswith("beep:"):
                    parts = preset.split(":")
                    freq = max(100, min(8000, int(parts[1]))) if len(parts) > 1 else 880
                    dur = max(30, min(1500, int(parts[2]))) if len(parts) > 2 else 180
                    if not dry_run:
                        threading.Thread(target=winsound.Beep, args=(freq, dur), daemon=True).start()
                    detail = f"Sound beep {freq}Hz {dur}ms"
                elif preset in ("chime", "alert", "success", "fanfare"):
                    notes = {
                        "chime": (880, 120),
                        "alert": (1046, 160),
                        "success": (1318, 150),
                        "fanfare": (1568, 220),
                    }[preset]
                    if not dry_run:
                        threading.Thread(target=winsound.Beep, args=notes, daemon=True).start()
                    detail = f"Soundboard preset {preset}"
                else:
                    if '"' in raw_s or "\r" in raw_s or "\n" in raw_s:
                        raise ValueError("Soundboard path contains unsupported quote or newline characters")
                    snd_path = Path(raw_s).expanduser()
                    if not snd_path.exists():
                        raise ValueError(f"Sound file not found: {snd_path}")
                    ext = snd_path.suffix.lower()
                    if not dry_run:
                        if ext == ".wav":
                            winsound.PlaySound(str(snd_path), winsound.SND_FILENAME | winsound.SND_ASYNC)
                        else:
                            try:
                                _mci_cmd("close hydro_sfx")
                            except ValueError:
                                pass
                            _mci_cmd(f'open "{snd_path}" alias hydro_sfx')
                            try:
                                _mci_cmd(f"setaudio hydro_sfx volume to {vol_pct * 10}")
                            except ValueError:
                                # Some codecs don't support setaudio; still report play errors below
                                pass
                            _mci_cmd("play hydro_sfx")
                    detail = f"Played {snd_path.name}"
                self._record(action_type, raw_s[:40], True, detail)
                return {"ok": True, "toast": "AUDIO SFX", "color": "#9E5CFF", "detail": detail}

            if action_type == "sequence":
                steps = payload if isinstance(payload, list) else json.loads(str(payload or "[]"))
                if not isinstance(steps, list) or not steps:
                    raise ValueError("Sequence macro requires non-empty step list")
                for st in steps:
                    kind = str(st.get("kind", "")).lower()
                    val = st.get("value", "")
                    if kind == "delay":
                        ms = max(0, min(5000, int(val or 50)))
                        if not dry_run:
                            time.sleep(ms / 1000.0)
                    else:
                        res = self.execute(kind, val, dry_run=dry_run)
                        if not res.get("ok"):
                            return res
                detail = f"Ran {len(steps)}-step sequence"
                self._record("sequence", f"{len(steps)} steps", True, detail)
                return {"ok": True, "toast": f"{len(steps)}x MACRO", "color": "#22E88A", "detail": detail}

            if action_type == "media":
                cmd = str(payload).strip().lower()
                if dry_run:
                    self._record(action_type, cmd, True, f"Dry media {cmd}")
                    return {"ok": True, "toast": cmd.upper()[:18], "color": "#22E88A", "detail": f"Dry media {cmd}"}
                if cmd in ("play", "play_pause"):
                    _send_vk_inputs([VK_MAP["PLAY_PAUSE"]])
                    self._record(action_type, cmd, True, "Media Play/Pause")
                    return {"ok": True, "toast": "PLAY / PAUSE", "color": "#22E88A", "detail": "Media Play/Pause"}
                if cmd == "next":
                    _send_vk_inputs([VK_MAP["MEDIA_NEXT"]])
                    self._record(action_type, cmd, True, "Media Next")
                    return {"ok": True, "toast": "NEXT TRACK", "color": "#3898FF", "detail": "Media Next"}
                if cmd == "prev":
                    _send_vk_inputs([VK_MAP["MEDIA_PREV"]])
                    self._record(action_type, cmd, True, "Media Prev")
                    return {"ok": True, "toast": "PREV TRACK", "color": "#3898FF", "detail": "Media Prev"}
                if cmd == "vol_up":
                    cur, _ = get_master_volume()
                    nv = set_master_volume(min(100, cur + 6))
                    self._record(action_type, cmd, True, f"Volume {nv}%")
                    return {"ok": True, "toast": f"VOL {nv}%", "color": "#3898FF", "detail": f"Volume {nv}%"}
                if cmd == "vol_down":
                    cur, _ = get_master_volume()
                    nv = set_master_volume(max(0, cur - 6))
                    self._record(action_type, cmd, True, f"Volume {nv}%")
                    return {"ok": True, "toast": f"VOL {nv}%", "color": "#3898FF", "detail": f"Volume {nv}%"}
                if cmd in ("mute", "mute_vol"):
                    m = toggle_master_mute()
                    self._record(action_type, cmd, True, f"Master mute={m}")
                    return {"ok": True, "toast": "MUTED" if m else "UNMUTED", "color": "#FF3A62" if m else "#22E88A", "detail": f"Master mute={m}"}
                if cmd == "mute_mic":
                    m = toggle_mic_mute()
                    self._record(action_type, cmd, True, f"Mic mute={m}")
                    return {"ok": True, "toast": "MIC MUTED" if m else "MIC LIVE", "color": "#FF3A62" if m else "#22E88A", "detail": f"Mic mute={m}", "toggle_state": m}
                raise ValueError(f"Unknown media command: {cmd}")

            if action_type == "volume":
                raw_v = payload
                mute_flag: bool | None = None
                delta: int | None = None
                target_pct: int | None = None
                if isinstance(raw_v, str) and raw_v.strip().startswith("{"):
                    raw_v = json.loads(raw_v)
                if isinstance(raw_v, dict):
                    if "mute" in raw_v:
                        mute_flag = bool(raw_v["mute"])
                    elif "delta" in raw_v:
                        delta = int(raw_v["delta"])
                    else:
                        target_pct = max(0, min(100, int(raw_v.get("value", raw_v.get("volume", 50)))))
                else:
                    sval = str(raw_v or "").strip().rstrip("%")
                    if not sval:
                        raise ValueError("volume requires percentage (0-100), +N/-N delta, or 'mute'")
                    if sval.lower() in ("mute", "toggle"):
                        mute_flag = True
                    elif sval.startswith(("+", "-")):
                        delta = int(sval)
                    else:
                        target_pct = max(0, min(100, int(float(sval))))
                if mute_flag is True:
                    m = False if dry_run else toggle_master_mute()
                    detail = "Volume mute toggled" + (" (dry)" if dry_run else f" -> {m}")
                    self._record(action_type, "mute", True, detail)
                    return {"ok": True, "toast": "MUTED" if m else "VOL MUTE", "color": "#FFB62E", "detail": detail}
                cur_v = 50 if dry_run else get_master_volume()[0]
                nv = max(0, min(100, (cur_v + delta) if delta is not None else int(target_pct or 0)))
                if not dry_run:
                    nv = set_master_volume(nv)
                detail = f"Volume -> {nv}%" + (" (dry)" if dry_run else "")
                self._record(action_type, str(nv), True, detail)
                return {"ok": True, "toast": f"VOL {nv}%", "color": "#3898FF", "detail": detail, "volume": nv}

            if action_type == "audio_device":
                flow = ""
                query = ""
                if isinstance(payload, str) and payload.strip().startswith("{"):
                    payload_obj = json.loads(payload)
                elif isinstance(payload, dict):
                    payload_obj = payload
                else:
                    payload_obj = {"device": str(payload or "")}
                if isinstance(payload_obj, dict):
                    query = str(payload_obj.get("device", payload_obj.get("name", payload_obj.get("id", "")))).strip()
                    flow = str(payload_obj.get("flow", "")).strip().lower()
                if not query:
                    raise ValueError("audio_device requires device name/id substring or 'cycle'/'list'")
                if flow and flow not in ("render", "capture", "all"):
                    raise ValueError(f"Unsupported audio_device flow: {flow}")
                devices = list_audio_devices(None if flow in ("", "all") else flow)
                if query.lower() == "list":
                    names = ", ".join(f"{d['name']} ({d['flow']}{'*' if d['default'] else ''})" for d in devices[:6])
                    detail = f"{len(devices)} active audio endpoints: {names}"
                    self._record(action_type, "list", True, detail)
                    return {"ok": True, "toast": f"{len(devices)} DEVICES", "color": "#00F0DA", "detail": detail, "devices": devices}
                if query.lower() in ("next", "cycle"):
                    pool = [d for d in devices if d["flow"] == (flow if flow in ("render", "capture") else "render")]
                    if not pool:
                        raise ValueError("No active audio endpoints available to cycle")
                    cur_idx = next((i for i, d in enumerate(pool) if d.get("default")), -1)
                    target_dev = pool[(cur_idx + 1) % len(pool)]
                    query = target_dev["id"]
                if dry_run:
                    ql = query.lower()
                    matched = next((d for d in devices if d["id"].lower() == ql or ql in d["name"].lower()), None)
                    lbl = (matched["name"] if matched else query)[:13].upper()
                    detail = (
                        f"Dry audio_device -> {matched['name']} ({matched['flow']})"
                        if matched
                        else f"Dry audio_device query '{query}' ({len(devices)} endpoints scanned)"
                    )
                    self._record(action_type, query[:30], True, detail)
                    return {"ok": True, "toast": f"OUT {lbl}"[:18], "color": "#22E88A", "detail": detail, "matched": matched, "devices": devices}
                chosen = set_default_audio_device(query, flow="" if flow == "all" else flow)
                detail = f"Default {chosen['flow']} audio -> {chosen['name']}"
                self._record(action_type, chosen["name"][:30], True, detail)
                return {"ok": True, "toast": f"OUT {chosen['name'][:13].upper()}", "color": "#22E88A", "detail": detail, "device": chosen}

            if action_type == "app_focus":
                cfg_focus: Any = payload
                if isinstance(payload, str) and payload.strip().startswith("{"):
                    try:
                        cfg_focus = json.loads(payload)
                    except json.JSONDecodeError as exc:
                        raise ValueError(f"Invalid app_focus JSON payload: {exc}") from exc
                if isinstance(cfg_focus, dict):
                    target = str(cfg_focus.get("target", "")).strip()
                    launch_cmd = str(cfg_focus.get("launch", "")).strip()
                else:
                    target = str(cfg_focus).strip()
                    launch_cmd = ""
                if not target:
                    raise ValueError("app_focus requires target process/title")
                focused = True if dry_run else _focus_window_by_target(target)
                if not focused and launch_cmd and not dry_run:
                    try:
                        os.startfile(launch_cmd)
                    except Exception:
                        argv = shlex.split(launch_cmd, posix=False)
                        if not argv:
                            raise ValueError("Invalid launch command")
                        subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    detail = f"Launched fallback {launch_cmd}"
                else:
                    detail = f"Focused {target}" if focused else f"No matching window for {target}"
                self._record(action_type, target[:30], focused or bool(launch_cmd), detail)
                return {
                    "ok": True if (focused or launch_cmd or dry_run) else False,
                    "toast": f"FOCUS {target[:11].upper()}" if focused else "NOT FOUND",
                    "color": "#00F0DA" if focused else "#FFB62E",
                    "detail": detail,
                }

            if action_type == "app":
                target = str(payload).strip()
                if not target:
                    raise ValueError("Empty application target")
                if not dry_run:
                    try:
                        os.startfile(target)  # handles executables, URIs (ms-settings:sound), shortcuts cleanly
                    except Exception:
                        argv = shlex.split(target, posix=False)
                        if not argv:
                            raise ValueError("Empty application command")
                        subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                detail = f"Launched {target}"
                self._record(action_type, target, True, detail)
                return {"ok": True, "toast": f"RUN {Path(target).stem[:12].upper()}", "color": "#00F0DA", "detail": detail}

            if action_type == "url":
                url = str(payload).strip()
                if not (url.startswith("http://") or url.startswith("https://")):
                    url = "https://" + url
                if not dry_run:
                    webbrowser.open(url)
                detail = f"Opened {url}"
                self._record(action_type, url, True, detail)
                return {"ok": True, "toast": "URL OPENED", "color": "#00F0DA", "detail": detail}

            if action_type == "shell":
                cmd = str(payload).strip()
                if not cmd:
                    raise ValueError("Empty shell command")
                if not dry_run:
                    subprocess.Popen(
                        ["powershell.exe", "-NoProfile", "-WindowStyle", "Hidden", "-Command", cmd],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                detail = f"PowerShell launched: {cmd[:48]}"
                self._record(action_type, cmd[:40], True, detail)
                return {"ok": True, "toast": "CMD EXEC", "color": "#FFB62E", "detail": detail}

            if action_type == "screenshot":
                self.screenshots_dir.mkdir(parents=True, exist_ok=True)
                out_file = self.screenshots_dir / f"hydro_shot_{time.strftime('%Y%m%d_%H%M%S')}.png"
                if not dry_run:
                    img = ImageGrab.grab(all_screens=True)
                    img.save(out_file)
                detail = f"Saved {out_file.name}"
                self._record(action_type, str(out_file), True, detail)
                return {"ok": True, "toast": "SHOT SAVED", "color": "#22E88A", "detail": detail}

            if action_type == "window":
                wcmd = str(payload).strip().lower()
                if wcmd == "snap_left":
                    if not dry_run:
                        _send_vk_inputs([VK_MAP["WIN"], VK_MAP["LEFT"]])
                    self._record(action_type, wcmd, True, "Win+Left")
                    return {"ok": True, "toast": "SNAP LEFT", "color": "#3898FF", "detail": "Win+Left"}
                if wcmd == "snap_right":
                    if not dry_run:
                        _send_vk_inputs([VK_MAP["WIN"], VK_MAP["RIGHT"]])
                    self._record(action_type, wcmd, True, "Win+Right")
                    return {"ok": True, "toast": "SNAP RIGHT", "color": "#3898FF", "detail": "Win+Right"}
                if wcmd == "snap_max":
                    if not dry_run:
                        _send_vk_inputs([VK_MAP["WIN"], VK_MAP["UP"]])
                    self._record(action_type, wcmd, True, "Win+Up")
                    return {"ok": True, "toast": "MAXIMIZE", "color": "#3898FF", "detail": "Win+Up"}
                if wcmd == "min_all":
                    if not dry_run:
                        _send_vk_inputs([VK_MAP["WIN"], VK_MAP["M"]])
                    self._record(action_type, wcmd, True, "Win+M")
                    return {"ok": True, "toast": "MIN ALL", "color": "#FFB62E", "detail": "Win+M"}
                if wcmd == "lock_pc":
                    if not dry_run:
                        user32.LockWorkStation()
                    self._record(action_type, wcmd, True, "LockWorkStation()")
                    return {"ok": True, "toast": "PC LOCKED", "color": "#FF3A62", "detail": "LockWorkStation()"}
                raise ValueError(f"Unknown window command: {wcmd}")

            if action_type == "webhook":
                if isinstance(payload, str) and payload.strip().startswith("{"):
                    cfg = json.loads(payload)
                elif isinstance(payload, dict):
                    cfg = payload
                else:
                    cfg = {"url": str(payload), "method": "POST", "body": "{}"}
                if not isinstance(cfg, dict):
                    raise ValueError("Webhook payload must be a URL or JSON object")
                url = str(cfg.get("url", "")).strip()
                method = str(cfg.get("method", "POST")).strip().upper()
                allow_private = bool(cfg.get("allow_private", False))
                if method not in ("GET", "HEAD", "POST", "PUT", "PATCH", "DELETE"):
                    raise ValueError(f"Unsupported webhook HTTP method: {method}")
                _validate_webhook_url(url, allow_private=allow_private)
                headers: dict[str, str] = {}
                raw_hdrs = cfg.get("headers")
                if raw_hdrs is not None and raw_hdrs != "":
                    if isinstance(raw_hdrs, str):
                        try:
                            raw_hdrs = json.loads(raw_hdrs)
                        except json.JSONDecodeError as exc:
                            raise ValueError(f"Webhook headers must be valid JSON object: {exc}") from exc
                    if not isinstance(raw_hdrs, dict):
                        raise ValueError("Webhook headers must be a JSON object of key/value pairs")
                    for k, v in raw_hdrs.items():
                        headers[str(k)] = str(v)
                raw_body = cfg.get("body")
                data_bytes: bytes | None = None
                if method not in ("GET", "HEAD"):
                    if isinstance(raw_body, (dict, list)):
                        data_bytes = json.dumps(raw_body).encode("utf-8")
                    elif raw_body is not None and str(raw_body) != "":
                        data_bytes = str(raw_body).encode("utf-8")
                    else:
                        data_bytes = b"{}"
                    if not any(hk.lower() == "content-type" for hk in headers):
                        headers["Content-Type"] = "application/json"
                status = 200
                if not dry_run:
                    req = urllib.request.Request(url, method=method, data=data_bytes, headers=headers)
                    if async_webhook:
                        def _fire() -> None:
                            try:
                                with urllib.request.urlopen(req, timeout=4.0) as resp:
                                    self._record(action_type, f"{method} {url}"[:40], True, f"Webhook {method} HTTP {resp.status}")
                            except Exception as exc:
                                self._record(action_type, f"{method} {url}"[:40], False, f"Webhook err: {exc}")

                        threading.Thread(target=_fire, daemon=True).start()
                        detail = f"Dispatched async {method} {url}"
                        self._record(action_type, f"{method} {url}"[:40], True, detail)
                        return {"ok": True, "toast": f"{method} SENT", "color": "#00F0DA", "detail": detail, "method": method, "headers": headers}
                    with urllib.request.urlopen(req, timeout=4.0) as resp:
                        status = resp.status
                detail = f"Webhook {method} HTTP {status}"
                self._record(action_type, f"{method} {url}"[:40], True, detail)
                return {"ok": True, "toast": f"HTTP {status}", "color": "#22E88A", "detail": detail, "method": method, "headers": headers}

            raise ValueError(f"Unsupported action_type: {action_type}")
        except Exception as exc:
            detail = str(exc)
            self._record(action_type, payload, False, detail)
            return {"ok": False, "toast": "MACRO ERR", "color": "#FF3A62", "err": True, "detail": detail}

