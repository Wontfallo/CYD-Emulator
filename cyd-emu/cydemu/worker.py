"""One powered-on virtual CYD. Spawned by the daemon; lives until power-off or re-flash.

Talks to the daemon over a multiprocessing connection: (method, args, kwargs) -> (ok, result).
"""
from __future__ import annotations

import argparse
import os
import sys
import traceback
from multiprocessing.connection import Client
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from cydemu.boards import get_board  # noqa: E402
from cydemu.machine import Machine  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--address", required=True)
    ap.add_argument("--board", required=True)
    ap.add_argument("--flash", required=True)
    ap.add_argument("--efuse", required=True)
    ap.add_argument("--icount", default="3")
    ap.add_argument("--wifi", action="store_true")
    ap.add_argument("--qemu-arg", action="append", default=[])
    a = ap.parse_args()

    conn = Client(a.address, authkey=bytes.fromhex(os.environ["CYDEMU_AUTHKEY"]))
    try:
        shift = None if a.icount in ("none", "off", "") else int(a.icount)
        m = Machine(get_board(a.board), Path(a.flash), Path(a.efuse), icount_shift=shift,
                    wifi=a.wifi, extra_args=a.qemu_arg)
    except Exception as exc:  # report the failure to the daemon, then die
        conn.send((False, f"{type(exc).__name__}: {exc}"))
        raise
    conn.send((True, {"qemu_args": m.qemu_args}))

    def touch_batch(events: list) -> int:
        for ev in events:
            if ev.get("raw"):
                m.touch_raw_event(ev["delay_ms"], ev["down"], ev["ch90"], ev["chD0"])
            else:
                m.touch_event(ev["delay_ms"], ev["down"], ev.get("x", 0), ev.get("y", 0), ev.get("rotation", -1))
        return len(events)

    methods = {
        "ping": lambda: "pong",
        "status": m.status,
        "gpio": m.gpio,
        "frame": m.frame,
        "touch_batch": touch_batch,
        "pin_event": m.pin_event,
        "set_adc_mv": m.set_adc_mv,
        "reset": m.reset,
        "events_pending": m.events_pending,
        "virtual_ms": m.virtual_ms,
        "uart_read": m.uart_read,
        "uart_write": m.uart_write,
    }
    while True:
        try:
            method, args, kwargs = conn.recv()
        except (EOFError, OSError):
            break
        if method == "exit":
            conn.send((True, None))
            break
        try:
            conn.send((True, methods[method](*args, **kwargs)))
        except Exception as exc:
            traceback.print_exc()
            conn.send((False, f"{type(exc).__name__}: {exc}"))
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0)     # QEMU's threads never return; leave without unwinding them


if __name__ == "__main__":
    main()
