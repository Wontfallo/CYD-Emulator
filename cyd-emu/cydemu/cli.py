"""cydemu command line: the way people and AI agents drive the virtual CYD."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from cydemu import __version__

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PORT = 8790


class CliError(Exception):
    pass


def state_dir(instance: str) -> Path:
    env = os.environ.get("CYDEMU_STATE")
    return Path(env) if env else ROOT / "state" / instance


def daemon_url(instance: str) -> str | None:
    try:
        return json.loads((state_dir(instance) / "daemon.json").read_text("utf-8"))["url"]
    except (OSError, ValueError, KeyError):
        return None


def request(url: str, method: str = "GET", body=None, timeout: float = 120.0, raw: bool = False, params: dict | None = None):
    if params:
        url += "?" + urllib.parse.urlencode({k: v for k, v in params.items() if v is not None})
    data, headers = None, {}
    if body is not None:
        if isinstance(body, (bytes, bytearray)):
            data, headers["Content-Type"] = bytes(body), "application/octet-stream"
        else:
            data, headers["Content-Type"] = json.dumps(body).encode(), "application/json"
    elif method == "POST":
        data = b""
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            payload = resp.read()
            if raw:
                return payload, dict(resp.headers)
            return json.loads(payload)
    except urllib.error.HTTPError as exc:
        try:
            msg = json.loads(exc.read()).get("error", str(exc))
        except ValueError:
            msg = str(exc)
        raise CliError(msg)


def alive(url: str | None) -> bool:
    if not url:
        return False
    try:
        request(url + "/boards", timeout=2)
        return True
    except (CliError, OSError):
        return False


def free_port(first: int) -> int:
    import socket
    for port in range(first, first + 50):
        with socket.socket() as sock:
            try:
                sock.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    raise CliError(f"no free port in {first}..{first + 49}")


def start_daemon(instance: str, port: int | None, board: str | None, icount: str | None, wifi: bool | None, foreground: bool = False) -> str:
    url = daemon_url(instance)
    if alive(url):
        return url
    sd = state_dir(instance)
    sd.mkdir(parents=True, exist_ok=True)
    port = port or free_port(DEFAULT_PORT)
    cmd = [sys.executable, "-m", "cydemu.daemon", "--state", str(sd), "--port", str(port)]
    if board:
        cmd += ["--board", board]
    if icount is not None:
        cmd += ["--icount", str(icount)]
    if wifi is not None:
        cmd += ["--wifi" if wifi else "--no-wifi"]
    if foreground:
        raise SystemExit(subprocess.call(cmd, cwd=str(ROOT)))
    pyw = Path(sys.executable).with_name("pythonw.exe")
    if pyw.exists():
        cmd[0] = str(pyw)
    flags = 0
    for name in ("DETACHED_PROCESS", "CREATE_NEW_PROCESS_GROUP", "CREATE_NO_WINDOW"):
        if name != "CREATE_NO_WINDOW" or not pyw.exists():
            flags |= getattr(subprocess, name, 0)
    log = open(sd / "daemon.log", "ab")
    subprocess.Popen(cmd, cwd=str(ROOT), stdin=subprocess.DEVNULL, stdout=log, stderr=log, creationflags=flags, close_fds=True)
    log.close()
    url = f"http://127.0.0.1:{port}"
    deadline = time.time() + 90
    while time.time() < deadline:
        if alive(url):
            return url
        time.sleep(0.2)
    tail = (sd / "daemon.log").read_bytes()[-800:].decode("utf-8", "replace")
    raise CliError(f"the cydemu daemon did not come up on port {port}.\n{tail}")


def fmt_status(s: dict) -> str:
    lines = [f"board     {s['board']}  ({s.get('board_description', '')})"]
    fl = s["flash"]
    if fl["apps"]:
        a = fl["apps"][0]
        lines.append(f"firmware  {a['project']} {a['version']}  built {a['compile_date']} {a['compile_time']}  idf {a['idf']}")
    else:
        lines.append("firmware  none (flash chip is empty)" if fl["blank"] else "firmware  no valid app image found")
    for w in fl.get("writes", []):
        lines.append(f"          {w['offset']:#08x}  {w['size']:>8} bytes  {w['label']:<12} {Path(w['source']).name if w['source'] else ''}")
    if not s["powered"]:
        lines.append("power     OFF" + (f"  ({s['last_exit']})" if s.get("last_exit") else ""))
        return "\n".join(lines)
    d = s["display"]
    lines.append(f"power     ON, boot #{s['boots']}, {s['virtual_ms'] / 1000:.1f}s of device time, {s['resets']} resets")
    screen = "not initialised" if not d["initialised"] else ("sleeping" if d["sleeping"] else ("off" if not d["on"] else "on"))
    lines.append(f"display   {d['width']}x{d['height']} {screen}, {d['pixels_written']} pixels drawn, idle {d['idle_virtual_ms']} ms")
    bl = s["backlight"]
    lines.append(f"backlight {bl['brightness'] * 100:.0f}% ({bl['mode']})")
    if "led" in s:
        lines.append(f"rgb led   {s['led']['hex']}")
    if "audio" in s:
        au = s["audio"]
        lines.append(f"audio     {'tone %d Hz' % au['tone_hz'] if au['tone_hz'] else 'silent'}, amp {'on' if au['amp_enabled'] else 'off'}")
    lines.append(f"touch     {'DOWN' if s['touch']['down'] else 'up'} ({s['touch']['transport']}, {s['touch']['conversions']} reads)")
    lines.append(f"serial    {s['uart']['tx_bytes']} bytes sent by firmware @ {s['uart']['baud']} baud (log offset {s['serial_end']})")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="cydemu", description="Virtual CYD (ESP32 Cheap Yellow Display): flash real firmware .bin files, look at the screen, touch it.")
    ap.add_argument("--instance", default="default", help="which virtual board (state/<instance>)")
    ap.add_argument("--json", action="store_true", help="print raw JSON")
    ap.add_argument("--version", action="version", version=f"cydemu {__version__}")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("start", help="start the emulator daemon")
    p.add_argument("--board"); p.add_argument("--port", type=int); p.add_argument("--icount")
    p.add_argument("--no-wifi", dest="wifi", action="store_false", default=None, help="leave out the Wi-Fi radio model")
    p.add_argument("--wifi", dest="wifi", action="store_true", help="include the Wi-Fi radio model (default)")
    p.add_argument("--foreground", action="store_true")
    sub.add_parser("stop", help="power off and stop the daemon")
    sub.add_parser("status", help="what the board is doing")
    sub.add_parser("boards", help="list board profiles")

    p = sub.add_parser("flash", help="flash a firmware (.bin app, merged image, or build directory) and boot it")
    p.add_argument("path"); p.add_argument("--bootloader"); p.add_argument("--partitions", help=".bin or .csv")
    p.add_argument("--boot-app0"); p.add_argument("--offset", help="write raw at this offset (e.g. 0x10000)")
    p.add_argument("--erase", action="store_true", help="erase the whole chip first (wipes NVS / filesystem)")
    p.add_argument("--board"); p.add_argument("--no-boot", action="store_true")
    sub.add_parser("erase", help="erase the whole flash chip")
    sub.add_parser("reset", help="press the EN (reset) button")
    p = sub.add_parser("power", help="power on / off / cycle"); p.add_argument("state", choices=["on", "off", "cycle"])

    p = sub.add_parser("screenshot", help="save the screen as a PNG")
    p.add_argument("out", nargs="?"); p.add_argument("--scale", type=int, default=2)
    p.add_argument("--settle", type=int, default=300, help="wait until the firmware has not drawn for this many ms (0 = now)")
    p.add_argument("--timeout", type=int, default=15000); p.add_argument("--rotation", default="auto")
    p.add_argument("--backlight", default="gate", choices=["gate", "scale", "ignore"])

    for name, dflt in (("tap", 100), ("hold", 800)):
        p = sub.add_parser(name, help=f"{name} the touchscreen at X Y (screen pixels)")
        p.add_argument("x", type=int); p.add_argument("y", type=int); p.add_argument("--ms", type=int, default=dflt)
    p = sub.add_parser("swipe", help="drag from X1 Y1 to X2 Y2")
    for n in ("x", "y", "x2", "y2"):
        p.add_argument(n, type=int)
    p.add_argument("--ms", type=int, default=300)
    p = sub.add_parser("touch", help="low level: down X Y / move X Y / up"); p.add_argument("action", choices=["down", "move", "up"])
    p.add_argument("x", type=int, nargs="?"); p.add_argument("y", type=int, nargs="?")
    p = sub.add_parser("button", help="click a hardware button (boot)"); p.add_argument("name", nargs="?", default="boot")
    p.add_argument("--ms", type=int, default=150); p.add_argument("--action", default="click", choices=["click", "press", "release"])

    p = sub.add_parser("serial", help="read the firmware's serial output")
    p.add_argument("--tail", type=int, help="last N bytes"); p.add_argument("--since", type=int, help="from log offset")
    p.add_argument("--all", action="store_true"); p.add_argument("--wait", type=int, default=0, help="wait up to MS for output")
    p.add_argument("--until", help="wait until this text appears"); p.add_argument("--follow", action="store_true")
    p = sub.add_parser("send", help="send text to the firmware's serial port")
    p.add_argument("text"); p.add_argument("--no-newline", action="store_true")
    p.add_argument("--expect", help="then wait for this text in the reply"); p.add_argument("--wait", type=int, default=1500)

    p = sub.add_parser("adc", help="set an analog input, e.g. 'adc battery 3700' (mV) or 'adc 34 1850'")
    p.add_argument("target"); p.add_argument("mv", type=float)
    p = sub.add_parser("gpio", help="show pins, or drive an input pin with --set GPIO LEVEL")
    p.add_argument("--set", nargs=2, type=int, metavar=("GPIO", "LEVEL"))
    p = sub.add_parser("wait", help="let MS of device time pass"); p.add_argument("ms", type=int)
    p = sub.add_parser("view", help="print the live viewer URL")
    p.add_argument("--open", action="store_true", help="open it in the default browser")

    # --json / --instance are accepted anywhere on the line, not only before the command
    argv = list(sys.argv[1:] if argv is None else argv)
    front: list[str] = []
    i = 0
    while i < len(argv):
        if argv[i] == "--json":
            front.append(argv.pop(i))
        elif argv[i] == "--instance" and i + 1 < len(argv):
            front += [argv.pop(i), argv.pop(i)]
        elif argv[i].startswith("--instance="):
            front.append(argv.pop(i))
        else:
            i += 1
    a = ap.parse_args(front + argv)
    for stream in (sys.stdout, sys.stderr):     # firmware output is arbitrary bytes; never crash printing it
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    try:
        return run(a)
    except CliError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


def run(a) -> int:
    inst = a.instance
    out = lambda obj, text=None: print(json.dumps(obj, indent=2) if a.json or text is None else text)  # noqa: E731

    if a.cmd == "start":
        url = start_daemon(inst, a.port, a.board, a.icount, a.wifi, a.foreground)
        if a.board:
            st = request(url + "/status")
            if st["board"] != a.board.lower():
                request(url + "/board", "POST", {"board": a.board})
        print(f"cydemu running at {url}  (live view: {url}/)")
        print(fmt_status(request(url + "/status")))
        return 0
    if a.cmd == "stop":
        url = daemon_url(inst)
        if not alive(url):
            print("cydemu is not running")
            return 0
        request(url + "/shutdown", "POST")
        print("cydemu stopped")
        return 0

    url = daemon_url(inst)
    if not alive(url):
        url = start_daemon(inst, None, getattr(a, "board", None), None, None)

    if a.cmd == "status":
        s = request(url + "/status")
        out(s, fmt_status(s))
    elif a.cmd == "boards":
        b = request(url + "/boards")
        out(b, "\n".join(f"{k:<26} {v}" for k, v in b.items()))
    elif a.cmd == "flash":
        body = {"path": str(Path(a.path).resolve()), "erase": a.erase, "boot": not a.no_boot, "board": a.board,
                "offset": a.offset,
                "bootloader": str(Path(a.bootloader).resolve()) if a.bootloader else None,
                "partitions": str(Path(a.partitions).resolve()) if a.partitions else None,
                "boot_app0": str(Path(a.boot_app0).resolve()) if a.boot_app0 else None}
        r = request(url + "/flash", "POST", body)
        out(r, "flashed.\n" + fmt_status(request(url + "/status")))
    elif a.cmd == "erase":
        r = request(url + "/erase", "POST")
        out(r, "flash erased; board powered off")
    elif a.cmd == "reset":
        r = request(url + "/reset", "POST")
        out(r, "reset")
    elif a.cmd == "power":
        r = request(url + "/power", "POST", {"on": a.state != "off", "cycle": a.state == "cycle"})
        out(r, f"power {a.state}")
    elif a.cmd == "screenshot":
        png, headers = request(url + "/screenshot.png", raw=True, timeout=a.timeout / 1000 + 30,
                               params={"scale": a.scale, "settle_ms": a.settle, "timeout_ms": a.timeout,
                                       "rotation": a.rotation, "backlight": a.backlight})
        if a.out:
            path = Path(a.out).resolve()
        else:
            shots = state_dir(inst) / "screenshots"
            shots.mkdir(parents=True, exist_ok=True)
            path = shots / f"screen-{time.strftime('%H%M%S')}-{int(time.time() * 1000) % 1000:03d}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(png)
        w, h, sc = headers.get("X-Cyd-Width"), headers.get("X-Cyd-Height"), headers.get("X-Cyd-Scale")
        notes = []
        if headers.get("X-Cyd-Pixels") == "0":
            notes.append("NOTE: the firmware has not drawn anything to the display yet.")
        elif headers.get("X-Cyd-Settled") != "true":
            notes.append("NOTE: the screen was still changing when this was taken (animation, or raise --timeout).")
        if headers.get("X-Cyd-Display-On") == "false":
            notes.append("NOTE: the display controller is off or asleep, so the panel is blank.")
        if float(headers.get("X-Cyd-Backlight") or 1) <= 0:
            notes.append("NOTE: the backlight is off, so the panel looks black (use --backlight ignore to see the pixels).")
        note = "".join("\n" + n for n in notes)
        info = {"path": str(path), "screen": [int(w), int(h)], "scale": int(sc),
                "settled": headers.get("X-Cyd-Settled") == "true", "notes": notes}
        out(info, f"{path}\nscreen is {w}x{h}; image is scaled x{sc}. Touch coordinates are screen pixels (image px / {sc}).{note}")
    elif a.cmd in ("tap", "hold"):
        r = request(url + "/touch", "POST", {"action": a.cmd, "x": a.x, "y": a.y, "duration_ms": a.ms})
        out(r, f"{a.cmd} at ({a.x}, {a.y}) for {a.ms} ms")
    elif a.cmd == "swipe":
        r = request(url + "/touch", "POST", {"action": "swipe", "x": a.x, "y": a.y, "x2": a.x2, "y2": a.y2, "duration_ms": a.ms})
        out(r, f"swipe ({a.x}, {a.y}) -> ({a.x2}, {a.y2}) over {a.ms} ms")
    elif a.cmd == "touch":
        body = {"action": a.action}
        if a.action != "up":
            if a.x is None or a.y is None:
                raise CliError(f"touch {a.action} needs X Y")
            body.update(x=a.x, y=a.y)
        r = request(url + "/touch", "POST", body)
        out(r, f"touch {a.action}")
    elif a.cmd == "button":
        r = request(url + "/button", "POST", {"name": a.name, "action": a.action, "duration_ms": a.ms})
        out(r, f"button {a.name} {a.action}")
    elif a.cmd == "serial":
        if a.follow:
            pos = request(url + "/serial", params={"tail": a.tail or 2000})
            sys.stdout.write(pos["data"]); sys.stdout.flush()
            nxt = pos["next"]
            try:
                while True:
                    r = request(url + "/serial", params={"since": nxt, "wait_ms": 1000})
                    sys.stdout.write(r["data"]); sys.stdout.flush()
                    nxt = r["next"]
            except KeyboardInterrupt:
                return 0
        params = {"wait_ms": a.wait, "until": a.until}
        if a.since is not None:
            params["since"] = a.since
        elif a.all:
            params["since"] = 0
        else:
            params["tail"] = a.tail or 4000
        r = request(url + "/serial", params=params, timeout=a.wait / 1000 + 30)
        out(r, r["data"].replace("\r\n", "\n") + (f"\n[log offset {r['next']}]" if not a.json else ""))
        if a.until and not r.get("matched"):
            print(f"error: '{a.until}' did not appear within {a.wait} ms", file=sys.stderr)
            return 2
    elif a.cmd == "send":
        w = request(url + "/serial", "POST", {"text": a.text, "newline": not a.no_newline})
        r = request(url + "/serial", params={"since": w["offset"], "wait_ms": a.wait, "until": a.expect}, timeout=a.wait / 1000 + 30)
        if not a.expect and a.wait:
            time.sleep(min(a.wait, 400) / 1000)
            r = request(url + "/serial", params={"since": w["offset"]})
        out(r, r["data"].replace("\r\n", "\n"))
        if a.expect and not r.get("matched"):
            print(f"error: '{a.expect}' did not appear within {a.wait} ms", file=sys.stderr)
            return 2
    elif a.cmd == "adc":
        body = {"mv": a.mv, **({"gpio": int(a.target)} if a.target.isdigit() else {"name": a.target})}
        r = request(url + "/adc", "POST", body)
        out(r, f"GPIO{r['gpio']} now sees {r['pin_mv']} mV")
    elif a.cmd == "gpio":
        if a.set:
            r = request(url + "/gpio", "POST", {"gpio": a.set[0], "level": a.set[1]})
            out(r, f"GPIO{a.set[0]} input driven {'high' if a.set[1] else 'low'}")
        else:
            pins = request(url + "/gpio")
            rows = [f"GPIO{p['gpio']:<2} {p['name']:<11} {p['mode']:<6} "
                    + (f"{p['level'] * 100:5.1f}% @ {p['freq_hz']} Hz" if p["mode"] == "pwm" else
                       (f"out {int(p['level'])}" if p["mode"] == "gpio" else
                        (f"matrix signal {p['signal']}" if p["mode"] == "periph" else f"in  {p['input_level']}")))
                    + (f"   ({p['writes']} writes)" if p["writes"] else "")
                    for p in pins if p["mode"] != "input" or p["name"] or p["writes"]]
            out(pins, "\n".join(rows))
    elif a.cmd == "wait":
        r = request(url + "/wait", "POST", {"virtual_ms": a.ms}, timeout=a.ms / 1000 * 30 + 60)
        out(r, f"{r['advanced_virtual_ms']} ms of device time passed")
    elif a.cmd == "view":
        print(url + "/")
        if a.open:
            import webbrowser
            webbrowser.open(url + "/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
