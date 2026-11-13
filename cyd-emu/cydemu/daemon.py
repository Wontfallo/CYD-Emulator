"""cydemu daemon: owns the virtual CYD (flash chip + powered worker) and serves the control API."""
from __future__ import annotations

import argparse
import json
import os
import secrets
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from multiprocessing.connection import Listener
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from cydemu.boards import BOARDS, DEFAULT_BOARD, get_board  # noqa: E402
from cydemu.flashimg import FlashChip, FlashError  # noqa: E402
from cydemu.png import encode_png  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SERIAL_CAP = 4 * 1024 * 1024
VIEWER = Path(__file__).resolve().parent / "viewer.html"


class ApiError(Exception):
    def __init__(self, message: str, code: int = 400):
        super().__init__(message)
        self.code = code


class Emulator:
    def __init__(self, state_dir: Path, board: str | None, icount: str | None, wifi: bool | None):
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        cfg = self._load_config()
        if board:
            cfg["board"] = board
        if icount is not None:
            cfg["icount"] = icount
        if wifi is not None:
            cfg["wifi"] = wifi
        self.cfg = cfg
        self.board = get_board(cfg["board"])
        self._save_config()

        self.chip = FlashChip(self.state_dir, self.board["flash_size"])
        self.chip.ensure_efuse()

        self.lock = threading.RLock()         # worker lifecycle + IPC
        self.proc: subprocess.Popen | None = None
        self.conn = None
        self.listener = None
        self.boot_count = 0
        self.powered_at = 0.0
        self.last_exit: str = ""

        self.serial = bytearray()
        self.serial_base = 0
        self.serial_cv = threading.Condition()
        self._pump = threading.Thread(target=self._pump_serial, daemon=True)
        self._pump.start()

    # ------------------------------------------------------------ config
    def _load_config(self) -> dict:
        cfg = {"board": DEFAULT_BOARD, "icount": "3", "wifi": True}
        try:
            cfg.update(json.loads((self.state_dir / "config.json").read_text("utf-8")))
        except (OSError, ValueError):
            pass
        return cfg

    def _save_config(self) -> None:
        (self.state_dir / "config.json").write_text(json.dumps(self.cfg, indent=2), "utf-8")

    def set_board(self, name: str) -> None:
        board = get_board(name)
        with self.lock:
            was_on = self.powered()
            self.power_off()
            self.board = board
            self.cfg["board"] = board["name"]
            self._save_config()
            if was_on:
                self.power_on()

    # ------------------------------------------------------------ power
    def powered(self) -> bool:
        return self.proc is not None and self.proc.poll() is None and self.conn is not None

    def power_on(self) -> dict:
        with self.lock:
            if self.powered():
                return {"powered": True}
            self._reap()
            if not self.chip.exists() or self.chip.describe()["blank"]:
                raise ApiError("the flash chip is empty: flash a firmware first (cydemu flash <file.bin>)", 409)
            address = rf"\\.\pipe\cydemu-{os.getpid()}-{secrets.token_hex(4)}"
            authkey = secrets.token_bytes(16)
            self.listener = Listener(address, family="AF_PIPE", authkey=authkey)
            log = open(self.state_dir / "worker.log", "ab")
            log.write(f"\n==== power on {time.strftime('%Y-%m-%d %H:%M:%S')} ====\n".encode())
            log.flush()
            cmd = [sys.executable, "-m", "cydemu.worker", "--address", address, "--board", self.board["name"],
                   "--flash", str(self.chip.path), "--efuse", str(self.chip.efuse_path),
                   "--icount", str(self.cfg.get("icount", "3"))]
            if self.cfg.get("wifi"):
                cmd.append("--wifi")
            env = dict(os.environ, CYDEMU_AUTHKEY=authkey.hex())
            self.proc = subprocess.Popen(cmd, cwd=str(ROOT), env=env, stdin=subprocess.DEVNULL, stdout=log,
                                         stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            log.close()

            accepted: list = []

            def accept() -> None:
                try:
                    accepted.append(self.listener.accept())
                except Exception as exc:  # listener closed, auth failure
                    accepted.append(exc)

            t = threading.Thread(target=accept, daemon=True)
            t.start()
            deadline = time.time() + 60
            while t.is_alive() and time.time() < deadline and self.proc.poll() is None:
                t.join(0.05)
            if not accepted or isinstance(accepted[0], Exception):
                self._kill()
                raise ApiError("the emulator worker failed to start:\n" + self._log_tail(), 500)
            self.conn = accepted[0]
            if not self.conn.poll(60):
                self._kill()
                raise ApiError("the emulator worker did not initialise:\n" + self._log_tail(), 500)
            ok, info = self.conn.recv()
            if not ok:
                self._kill()
                raise ApiError(f"QEMU failed to start: {info}\n" + self._log_tail(), 500)
            self.boot_count += 1
            self.powered_at = time.time()
            self.last_exit = ""
            self._serial_append(f"\r\n[cydemu] power on #{self.boot_count}\r\n".encode())
            return {"powered": True, **info}

    def _log_tail(self, n: int = 1500) -> str:
        try:
            return (self.state_dir / "worker.log").read_bytes()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def _reap(self) -> None:
        if self.proc is not None and self.proc.poll() is not None:
            self.last_exit = f"worker exited with code {self.proc.returncode}"
            self._kill()

    def _kill(self) -> None:
        if self.conn is not None:
            try:
                self.conn.close()
            except OSError:
                pass
        if self.listener is not None:
            try:
                self.listener.close()
            except OSError:
                pass
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                pass
        self.proc, self.conn, self.listener = None, None, None

    def power_off(self) -> None:
        with self.lock:
            if self.powered():
                try:
                    self.conn.send(("exit", (), {}))
                    if self.conn.poll(2):
                        self.conn.recv()
                    self.proc.wait(3)
                except (OSError, EOFError, subprocess.TimeoutExpired):
                    pass
                self._serial_append(b"\r\n[cydemu] power off\r\n")
            self._kill()

    def call(self, method: str, *args, **kwargs):
        with self.lock:
            if not self.powered():
                self._reap()
                why = f" ({self.last_exit}; see {self.state_dir / 'worker.log'})" if self.last_exit else ""
                raise ApiError("the virtual CYD is powered off" + why + ". Flash a firmware or POST /power.", 409)
            try:
                self.conn.send((method, args, kwargs))
                if not self.conn.poll(30):
                    raise ApiError("the emulator worker stopped responding", 500)
                ok, result = self.conn.recv()
            except (OSError, EOFError):
                self._reap()
                self._kill()
                raise ApiError("the emulator worker died:\n" + self._log_tail(), 500)
            if not ok:
                raise ApiError(str(result), 400)
            return result

    # ------------------------------------------------------------ serial
    def _serial_append(self, data: bytes) -> None:
        with self.serial_cv:
            self.serial += data
            over = len(self.serial) - SERIAL_CAP
            if over > 0:
                del self.serial[:over]
                self.serial_base += over
            self.serial_cv.notify_all()

    def _pump_serial(self) -> None:
        while True:
            time.sleep(0.02)
            if not self.powered():
                time.sleep(0.1)
                continue
            try:
                data = self.call("uart_read")
            except ApiError:
                continue
            if data:
                self._serial_append(data)

    def serial_end(self) -> int:
        with self.serial_cv:
            return self.serial_base + len(self.serial)

    def serial_read(self, since: int | None, wait_ms: int = 0, until: str | None = None, limit: int = 1 << 20) -> dict:
        deadline = time.time() + wait_ms / 1000.0
        needle = until.encode() if until else None
        with self.serial_cv:
            end = self.serial_base + len(self.serial)
            if since is None:
                since = end                     # only what arrives from now on
            elif since < 0:
                since = end + since             # the last N bytes
            since = max(since, self.serial_base)
            while True:
                end = self.serial_base + len(self.serial)
                chunk = bytes(self.serial[since - self.serial_base:])
                found = needle is not None and needle in chunk
                if found or (needle is None and chunk and wait_ms >= 0) or time.time() >= deadline:
                    break
                self.serial_cv.wait(max(0.0, min(0.25, deadline - time.time())))
            chunk = chunk[:limit]
            return {"offset": since, "next": since + len(chunk), "end": end,
                    "data": chunk.decode("utf-8", "replace"), **({"matched": found} if needle else {})}

    # ------------------------------------------------------------ waits
    def wait_virtual(self, ms: int, timeout_s: float = 30.0) -> int:
        start = self.call("virtual_ms")
        deadline = time.time() + timeout_s
        now = start
        while now < start + ms and time.time() < deadline:
            time.sleep(0.01)
            now = self.call("virtual_ms")
        return now - start

    def wait_events(self, timeout_s: float) -> bool:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.call("events_pending") == 0:
                return True
            time.sleep(0.01)
        return False

    def wait_settled(self, settle_ms: int, timeout_ms: int) -> bool:
        """Block until the firmware has stopped drawing for settle_ms of *virtual* time."""
        deadline = time.time() + timeout_ms / 1000.0
        while True:
            d = self.call("status")["display"]
            if d["idle_virtual_ms"] >= settle_ms:
                return True
            if time.time() >= deadline:
                return False
            time.sleep(0.02)

    # ------------------------------------------------------------ actions
    def status(self) -> dict:
        out: dict = {"board": self.board["name"], "board_description": self.board.get("description", ""),
                     "powered": self.powered(), "boots": self.boot_count,
                     "icount": self.cfg.get("icount"), "wifi": bool(self.cfg.get("wifi")),
                     "flash": self.chip.describe(), "serial_end": self.serial_end(),
                     "state_dir": str(self.state_dir)}
        if not self.powered():
            self._reap()
            if self.last_exit:
                out["last_exit"] = self.last_exit
            return out
        out.update(self.call("status"))
        out["uptime_s"] = round(time.time() - self.powered_at, 1)
        return out

    def flash(self, body: dict) -> dict:
        if not body.get("path"):
            raise ApiError("flash needs 'path' (a .bin file or a build directory)")
        with self.lock:
            if body.get("board"):
                self.power_off()
                self.board = get_board(body["board"])
                self.cfg["board"] = self.board["name"]
                self._save_config()
                self.chip = FlashChip(self.state_dir, self.board["flash_size"])
            self.power_off()
            try:
                desc = self.chip.flash(body["path"], offset=_int(body.get("offset")), bootloader=body.get("bootloader"),
                                       partitions=body.get("partitions"), boot_app0=body.get("boot_app0"),
                                       erase=bool(body.get("erase")))
            except (FlashError, OSError) as exc:
                raise ApiError(f"flash failed: {exc}")
            if body.get("boot", True):
                self.power_on()
            return {"flash": desc, "powered": self.powered()}

    def screenshot(self, q: dict) -> tuple[bytes, dict]:
        rotation = -1 if q.get("rotation", "auto") in ("auto", "-1", "") else int(q["rotation"])
        settle = int(q.get("settle_ms", 0))
        settled = True
        if settle > 0:
            settled = self.wait_settled(settle, int(q.get("timeout_ms", 10000)))
        w, h, rgb = self.call("frame", rotation, q.get("backlight", "gate"))
        st = self.call("status")
        scale = max(1, min(8, int(q.get("scale", 1))))
        return encode_png(w, h, rgb, scale), {
            "X-Cyd-Width": str(w), "X-Cyd-Height": str(h), "X-Cyd-Scale": str(scale),
            "X-Cyd-Settled": str(settled).lower(),
            "X-Cyd-Pixels": str(st["display"]["pixels_written"]),
            "X-Cyd-Display-On": str(st["display"]["on"] and not st["display"]["sleeping"]).lower(),
            "X-Cyd-Backlight": str(st["backlight"]["brightness"]),
        }

    def touch(self, body: dict) -> dict:
        action = body.get("action", "tap")
        rot = -1 if body.get("rotation", "auto") in ("auto", None) else int(body["rotation"])

        def pt(prefix: str = "") -> dict:
            if body.get("raw"):
                return {"raw": True, "ch90": int(body[f"ch90{prefix}"]), "chD0": int(body[f"chD0{prefix}"])}
            if f"x{prefix}" not in body or f"y{prefix}" not in body:
                raise ApiError(f"touch '{action}' needs x{prefix} and y{prefix}")
            return {"x": int(body[f"x{prefix}"]), "y": int(body[f"y{prefix}"]), "rotation": rot}

        events: list[dict] = []
        total = 0
        if action in ("tap", "hold"):
            total = int(body.get("duration_ms", 100 if action == "tap" else 800))
            events = [{"delay_ms": 0, "down": True, **pt()}, {"delay_ms": total, "down": False}]
        elif action == "swipe":
            total = int(body.get("duration_ms", 300))
            a, b = pt(), pt("2")
            steps = max(2, int(body.get("steps", max(2, total // 10))))
            for i in range(steps + 1):
                f = i / steps
                events.append({"delay_ms": int(total * f), "down": True, "rotation": rot,
                               "x": round(a["x"] + (b["x"] - a["x"]) * f), "y": round(a["y"] + (b["y"] - a["y"]) * f)})
            total += 30
            events.append({"delay_ms": total, "down": False})
        elif action in ("down", "move"):
            events = [{"delay_ms": int(body.get("delay_ms", 0)), "down": True, **pt()}]
        elif action == "up":
            events = [{"delay_ms": int(body.get("delay_ms", 0)), "down": False}]
        else:
            raise ApiError(f"unknown touch action '{action}' (tap, hold, swipe, down, move, up)")
        self.call("touch_batch", events)
        done = True
        if body.get("wait", True):
            done = self.wait_events(total / 1000.0 * 20 + 10)
            self.wait_virtual(int(body.get("after_ms", 60)), 10)
        return {"action": action, "events": len(events), "completed": done}

    def button(self, body: dict) -> dict:
        name = body.get("name", "boot")
        gpio = self.board["pins"].get(name)
        if gpio is None:
            raise ApiError(f"this board has no '{name}' button")
        action = body.get("action", "click")
        if action == "click":
            ms = int(body.get("duration_ms", 150))
            self.call("pin_event", 0, gpio, 0)
            self.call("pin_event", ms, gpio, 1)
            self.wait_events(ms / 1000.0 * 20 + 10)
            self.wait_virtual(40, 10)
        elif action in ("press", "release"):
            self.call("pin_event", 0, gpio, 0 if action == "press" else 1)
            self.wait_events(5)
        else:
            raise ApiError("button action must be click, press or release")
        return {"button": name, "gpio": gpio, "action": action}

    def adc(self, body: dict) -> dict:
        if "mv" not in body:
            raise ApiError("adc needs 'mv'")
        mv = float(body["mv"])
        if "name" in body:
            spec = (self.board.get("adc") or {}).get(body["name"])
            if not spec:
                raise ApiError(f"this board has no ADC input named '{body['name']}'")
            gpio, pin_mv = spec["gpio"], mv / spec.get("divider", 1.0)
        else:
            gpio, pin_mv = int(body["gpio"]), mv
        self.call("set_adc_mv", gpio, pin_mv)
        return {"gpio": gpio, "pin_mv": round(pin_mv, 1)}


def _int(v):
    if v is None or v == "":
        return None
    return int(v, 0) if isinstance(v, str) else int(v)


class Handler(BaseHTTPRequestHandler):
    emu: Emulator
    server_version = "cydemu/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # quiet
        pass

    def _send(self, code: int, body: bytes, ctype: str, headers: dict | None = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code: int = 200) -> None:
        self._send(code, json.dumps(obj).encode(), "application/json")

    def _body(self) -> bytes:
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def _body_json(self) -> dict:
        raw = self._body()
        if not raw:
            return {}
        try:
            obj = json.loads(raw)
        except ValueError:
            raise ApiError("request body is not valid JSON")
        if not isinstance(obj, dict):
            raise ApiError("request body must be a JSON object")
        return obj

    def _guard(self, fn) -> None:
        try:
            fn()
        except ApiError as exc:
            self._json({"error": str(exc)}, exc.code)
        except (KeyError, ValueError, TypeError) as exc:
            self._json({"error": f"bad request: {exc}"}, 400)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self) -> None:
        self._guard(self._get)

    def do_POST(self) -> None:
        self._guard(self._post)

    def _get(self) -> None:
        url = urlparse(self.path)
        q = {k: v[-1] for k, v in parse_qs(url.query).items()}
        e = self.emu
        if url.path in ("/", "/index.html"):
            self._send(200, VIEWER.read_bytes(), "text/html; charset=utf-8")
        elif url.path == "/status":
            self._json(e.status())
        elif url.path == "/screenshot.png":
            png, headers = e.screenshot(q)
            self._send(200, png, "image/png", headers)
        elif url.path == "/serial":
            since = int(q["since"]) if "since" in q else (-int(q["tail"]) if "tail" in q else None)
            self._json(e.serial_read(since, int(q.get("wait_ms", 0)), q.get("until")))
        elif url.path == "/gpio":
            self._json(e.call("gpio"))
        elif url.path == "/boards":
            self._json({k: v.get("description", "") for k, v in BOARDS.items()})
        else:
            self._json({"error": f"no such endpoint: GET {url.path}"}, 404)

    def _post(self) -> None:
        url = urlparse(self.path)
        e = self.emu
        if url.path == "/serial":
            ctype = self.headers.get("Content-Type", "")
            if "json" in ctype:
                body = self._body_json()
                data = str(body.get("text", "")).encode()
                if body.get("newline", True) and not data.endswith(b"\n"):
                    data += b"\n"
            else:
                data = self._body()
            start = e.serial_end()
            self._json({"written": e.call("uart_write", data), "offset": start})
            return
        body = self._body_json()
        if url.path == "/flash":
            self._json(e.flash(body))
        elif url.path == "/erase":
            with e.lock:
                e.power_off()
                self._json({"flash": e.chip.erase(), "powered": False})
        elif url.path == "/reset":
            if e.powered():
                e.call("reset")
                self._json({"reset": "EN pulse"})
            else:
                self._json(e.power_on())
        elif url.path == "/power":
            if body.get("cycle"):
                e.power_off()
                self._json(e.power_on())
            elif body.get("on", True):
                self._json(e.power_on())
            else:
                e.power_off()
                self._json({"powered": False})
        elif url.path == "/touch":
            self._json(e.touch(body))
        elif url.path == "/button":
            self._json(e.button(body))
        elif url.path == "/adc":
            self._json(e.adc(body))
        elif url.path == "/gpio":
            e.call("pin_event", int(body.get("delay_ms", 0)), int(body["gpio"]), int(body["level"]))
            self._json({"gpio": int(body["gpio"]), "level": int(body["level"])})
        elif url.path == "/wait":
            self._json({"advanced_virtual_ms": e.wait_virtual(int(body.get("virtual_ms", 100)), float(body.get("timeout_s", 60)))})
        elif url.path == "/board":
            e.set_board(body["board"])
            self._json({"board": e.board["name"]})
        elif url.path == "/shutdown":
            self._json({"shutdown": True})
            threading.Thread(target=self.server.shutdown, daemon=True).start()
        else:
            self._json({"error": f"no such endpoint: POST {url.path}"}, 404)


def main() -> None:
    ap = argparse.ArgumentParser(description="cydemu daemon")
    ap.add_argument("--state", required=True)
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--board", default=None)
    ap.add_argument("--icount", default=None)
    ap.add_argument("--wifi", dest="wifi", action="store_true", default=None)
    ap.add_argument("--no-wifi", dest="wifi", action="store_false")
    a = ap.parse_args()

    state = Path(a.state)
    emu = Emulator(state, a.board, a.icount, a.wifi)
    Handler.emu = emu
    httpd = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    httpd.daemon_threads = True
    info = state / "daemon.json"
    info.write_text(json.dumps({"pid": os.getpid(), "port": a.port, "url": f"http://127.0.0.1:{a.port}"}), "utf-8")
    try:
        if emu.chip.exists() and not emu.chip.describe()["blank"]:
            try:
                emu.power_on()
            except ApiError as exc:
                print(f"power on failed: {exc}", file=sys.stderr, flush=True)
        httpd.serve_forever()
    finally:
        emu.power_off()
        try:
            info.unlink()
        except OSError:
            pass


if __name__ == "__main__":
    main()
