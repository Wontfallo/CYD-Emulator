"""Link to a HYDRO-DECK firmware running inside the cydemu virtual CYD (../../cyd-emu).

Same job as SerialBridge, but the "USB cable" is the emulator's serial endpoint:
device -> PC lines are long-polled from GET /serial, PC -> device packets go to POST /serial.
If no emulator is running this bridge just keeps looking for one.
"""
from __future__ import annotations

import json
import os
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Callable

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DAEMON_INFO = REPO_ROOT / "cyd-emu" / "state" / "default" / "daemon.json"
DEFAULT_URL = "http://127.0.0.1:8790"


def discover_url() -> str:
    """CYDEMU_URL wins; otherwise wherever the default emulator instance says it is listening."""
    env = os.environ.get("CYDEMU_URL", "").strip()
    if env:
        return env.rstrip("/")
    try:
        return str(json.loads(DAEMON_INFO.read_text("utf-8"))["url"]).rstrip("/")
    except (OSError, ValueError, KeyError):
        return DEFAULT_URL


class CydEmuBridge:
    def __init__(self, on_event: Callable[[dict[str, Any]], None]):
        self.on_event = on_event
        self.url: str = ""
        self.connected: bool = False
        self.hydro_handshake: bool = False
        self.device_info: dict[str, Any] = {}
        self.last_error: str = ""
        self.rx_lines: int = 0
        self.tx_packets: int = 0
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()

    # ------------------------------------------------------------ lifecycle
    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run_loop, name="hydro_cydemu", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self.connected = False
        self.hydro_handshake = False

    # ------------------------------------------------------------ io
    def _get(self, path: str, timeout: float) -> dict[str, Any]:
        with urllib.request.urlopen(self.url + path, timeout=timeout) as resp:
            return json.loads(resp.read())

    def _post_line(self, line: bytes) -> None:
        req = urllib.request.Request(self.url + "/serial", data=line, method="POST",
                                     headers={"Content-Type": "application/octet-stream"})
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            resp.read()

    def send_packet(self, payload: dict[str, Any]) -> bool:
        if not self.connected:
            return False
        try:
            self._post_line((json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8"))
            with self._lock:
                self.tx_packets += 1
            return True
        except (OSError, ValueError) as exc:
            self.last_error = str(exc)
            return False

    def diagnostics(self) -> dict[str, Any]:
        if not self.connected:
            hint = "No virtual CYD running (start one: cyd-emu\\cydemu.cmd flash <firmware.bin>)"
        elif not self.hydro_handshake:
            hint = "Virtual CYD found; waiting for a HYDRO-DECK ready packet"
        else:
            hint = "OK"
        return {
            "url": self.url,
            "connected": self.connected,
            "hydro_handshake": self.hydro_handshake,
            "device_info": dict(self.device_info),
            "rx_lines": self.rx_lines,
            "tx_packets": self.tx_packets,
            "last_error": self.last_error,
            "diagnostic": hint,
        }

    # ------------------------------------------------------------ loop
    def _run_loop(self) -> None:
        offset = 0
        pending = ""
        last_ping = 0.0
        while not self._stop.is_set():
            if not self.connected:
                self.url = discover_url()
                try:
                    status = self._get("/status", 2.0)
                    if not status.get("powered"):
                        raise OSError("virtual CYD is powered off")
                    offset = int(status.get("serial_end", 0))     # only what the firmware says from now on
                    pending = ""
                    self.connected = True
                    self.hydro_handshake = False
                    self.last_error = ""
                    last_ping = 0.0
                except (OSError, ValueError) as exc:
                    self.last_error = str(exc)
                    self._stop.wait(2.0)
                    continue

            try:
                if not self.hydro_handshake and time.monotonic() - last_ping > 2.5:
                    last_ping = time.monotonic()
                    self._post_line(b'{"t":"ping"}\n')
                chunk = self._get(f"/serial?since={offset}&wait_ms=500", 5.0)
                if chunk.get("offset", offset) > offset:
                    pending = ""                                   # log was trimmed under us
                offset = int(chunk.get("next", offset))
                pending += chunk.get("data", "")
                while "\n" in pending:
                    raw, pending = pending.split("\n", 1)
                    raw = raw.strip()
                    if not raw:
                        continue
                    self.rx_lines += 1
                    if "[cydemu] power on" in raw:
                        self.hydro_handshake = False               # board was re-flashed or power-cycled
                        continue
                    if not raw.startswith("{"):
                        continue
                    try:
                        msg = json.loads(raw)
                    except ValueError:
                        continue
                    if not isinstance(msg, dict):
                        continue
                    if msg.get("t") == "ready":
                        self.hydro_handshake = True
                        self.device_info = msg
                    self.on_event(msg)
            except (OSError, ValueError) as exc:
                self.last_error = str(exc)
                self.connected = False
                self.hydro_handshake = False
                self._stop.wait(1.0)
