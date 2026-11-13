from __future__ import annotations

import json
import queue
import threading
import time
from typing import Any, Callable

import serial
import serial.tools.list_ports


def list_serial_ports() -> list[dict[str, str]]:
    ports = []
    for p in serial.tools.list_ports.comports():
        ports.append({
            "device": p.device,
            "description": p.description or "",
            "hwid": p.hwid or "",
            "is_ch340": ("1A86" in (p.hwid or "").upper() or "CH340" in (p.description or "").upper()),
        })
    ports.sort(key=lambda x: (not x["is_ch340"], x["device"]))
    return ports


class SerialBridge:
    def __init__(self, on_event: Callable[[dict[str, Any]], None]):
        self.on_event = on_event
        self.port_name: str = "COM11"
        self.baud_rate: int = 115200
        self.connected: bool = False
        self.hydro_handshake: bool = False
        self.device_info: dict[str, Any] = {}
        self.last_error: str = ""
        self.rx_lines: int = 0
        self.tx_packets: int = 0
        self.last_rx_ts: float = 0.0
        self.recent_logs: list[str] = []

        self._tx_queue: queue.Queue[str] = queue.Queue(maxsize=64)
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._ser: serial.Serial | None = None
        self._reconnect_req = threading.Event()
        self._paused = False
        self._lock = threading.RLock()

    def _log(self, msg: str) -> None:
        line = f"[{time.strftime('%H:%M:%S')}] {msg}"
        with self._lock:
            self.recent_logs.insert(0, line)
            del self.recent_logs[60:]

    def start(self, port_name: str = "COM11", baud_rate: int = 115200) -> None:
        with self._lock:
            self.port_name = port_name
            self.baud_rate = baud_rate
            self._paused = False
        if self._thread and self._thread.is_alive():
            self._reconnect_req.set()
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run_loop, name="hydro_serial", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._reconnect_req.set()
        if self._ser and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass

    def disconnect(self) -> None:
        with self._lock:
            self._paused = True
            self.connected = False
            self.hydro_handshake = False
            self.last_error = "Released by Studio"
        self._reconnect_req.set()
        if self._ser and self._ser.is_open:
            try:
                self._ser.close()
            except Exception:
                pass
        self._log("Port released cleanly")

    def configure(self, port_name: str, baud_rate: int = 115200) -> None:
        with self._lock:
            self.port_name = port_name
            self.baud_rate = baud_rate
            self._paused = False
        if not (self._thread and self._thread.is_alive()):
            self.start(port_name, baud_rate)
            return
        self._reconnect_req.set()

    def send_packet(self, payload: dict[str, Any]) -> bool:
        if not self.connected:
            return False
        try:
            line = json.dumps(payload, separators=(",", ":")) + "\n"
            if self._tx_queue.full():
                try:
                    self._tx_queue.get_nowait()
                except queue.Empty:
                    pass
            self._tx_queue.put_nowait(line)
            return True
        except Exception:
            return False

    def diagnostics(self) -> dict[str, Any]:
        with self._lock:
            boot_hint = "OK"
            if self._paused:
                boot_hint = "Disconnected (COM port released for flashing)"
            elif not self.connected:
                boot_hint = "Port closed / unavailable"
            elif not self.hydro_handshake:
                boot_hint = "Port open; awaiting HYDRO-DECK firmware ready packet (check P2 UART / COM11 flash)"
            return {
                "port": self.port_name,
                "baud": self.baud_rate,
                "connected": self.connected,
                "paused": self._paused,
                "hydro_handshake": self.hydro_handshake,
                "device_info": dict(self.device_info),
                "rx_lines": self.rx_lines,
                "tx_packets": self.tx_packets,
                "last_error": self.last_error,
                "diagnostic": boot_hint,
                "recent_logs": list(self.recent_logs[:20]),
            }

    def _run_loop(self) -> None:
        buf = bytearray()
        last_ping = 0.0
        while not self._stop.is_set():
            with self._lock:
                paused = self._paused
                port_name = self.port_name
                baud_rate = self.baud_rate
            if paused:
                self.connected = False
                self.hydro_handshake = False
                if self._ser and self._ser.is_open:
                    try:
                        self._ser.close()
                    except Exception:
                        pass
                self._reconnect_req.wait(timeout=0.5)
                self._reconnect_req.clear()
                continue

            if not self.connected or self._reconnect_req.is_set():
                self._reconnect_req.clear()
                if self._ser and self._ser.is_open:
                    try:
                        self._ser.close()
                    except Exception:
                        pass
                self.connected = False
                self.hydro_handshake = False
                if not port_name:
                    self._stop.wait(1.0)
                    continue
                try:
                    ser = serial.Serial()
                    ser.port = port_name
                    ser.baudrate = baud_rate
                    ser.timeout = 0.03
                    # A deck/config packet can be several KB; at 115200 baud that is hundreds of
                    # milliseconds on the wire, so the write must be allowed to finish.
                    ser.write_timeout = 3.0
                    ser.dsrdtr = False
                    ser.rtscts = False
                    ser.dtr = False
                    ser.rts = False
                    ser.open()
                    ser.dtr = False
                    ser.rts = False
                    self._ser = ser
                    self.connected = True
                    self.last_error = ""
                    self.last_rx_ts = time.monotonic()
                    buf.clear()
                    self._log(f"Opened {port_name} @ {baud_rate} (DTR/RTS held LOW)")
                    self.send_packet({"t": "ping"})
                except Exception as exc:
                    self.last_error = str(exc)
                    self.connected = False
                    self._stop.wait(2.0)
                    continue

            try:
                assert self._ser is not None
                # Drain outbound queue
                while not self._tx_queue.empty():
                    line = self._tx_queue.get_nowait()
                    self._ser.write(line.encode("utf-8"))
                    self.tx_packets += 1

                # Detect stale link (>10s without any RX line)
                if self.hydro_handshake and self.last_rx_ts > 0 and (time.monotonic() - self.last_rx_ts > 10.0):
                    self.hydro_handshake = False
                    self._log("Link stale (>10s RX timeout); re-pinging")

                # Periodic ping until firmware confirms ready
                if not self.hydro_handshake and (time.monotonic() - last_ping > 2.5):
                    last_ping = time.monotonic()
                    self._ser.write(b'{"t":"ping"}\n')

                waiting = self._ser.in_waiting
                chunk = self._ser.read(waiting if waiting > 0 else 1)
                if chunk:
                    buf.extend(chunk)
                    while b"\n" in buf:
                        idx = buf.index(b"\n")
                        raw_line = buf[:idx].decode("utf-8", errors="ignore").strip()
                        del buf[: idx + 1]
                        if not raw_line:
                            continue
                        self.rx_lines += 1
                        self.last_rx_ts = time.monotonic()
                        if raw_line.startswith("{"):
                            try:
                                msg = json.loads(raw_line)
                                if msg.get("t") == "ready":
                                    self.hydro_handshake = True
                                    self.device_info = msg
                                    self._log(f"HYDRO-DECK ready: {msg.get('panel')} SRAM={msg.get('sram')}")
                                self.on_event(msg)
                            except Exception:
                                self._log(f"RX invalid JSON: {raw_line[:80]}")
                        else:
                            self._log(f"RX log: {raw_line[:96]}")
            except Exception as exc:
                self.last_error = str(exc)
                self._log(f"Serial error: {exc}")
                self.connected = False
                self.hydro_handshake = False
                self._stop.wait(1.0)

