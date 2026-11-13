"""End to end: flash a real firmware .bin into the virtual CYD and use it the way an agent would.

Everything goes through the `cydemu` command line, against a throwaway board in a temp dir.
The firmware under test is the HYDRO-DECK build in ../firmware_dist; nothing in the emulator
is specific to it.
"""
from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT.parent / "firmware_dist" / "HYDRO-DECK_E32R28T_ILI9341_firmware.bin"

pytestmark = pytest.mark.skipif(
    not APP.exists() or not (ROOT / "vendor" / "qemu" / "libqemu-xtensa.dll").exists() or not (ROOT / "core" / "cyd_core.dll").exists(),
    reason="needs firmware_dist, vendor/qemu and a built core/cyd_core.dll",
)


class Board:
    def __init__(self, state: Path):
        self.env = dict(os.environ, CYDEMU_STATE=str(state), PYTHONPATH=str(ROOT))

    def run(self, *args: str, ok: bool = True) -> str:
        p = subprocess.run([sys.executable, "-m", "cydemu", *args], cwd=str(ROOT), env=self.env,
                           capture_output=True, text=True, errors="replace", timeout=240)
        if ok:
            assert p.returncode == 0, f"cydemu {' '.join(args)} failed:\n{p.stdout}\n{p.stderr}"
        return p.stdout

    def status(self) -> dict:
        return json.loads(self.run("status", "--json"))

    def serial_offset(self) -> int:
        return self.status()["serial_end"]

    def expect(self, text: str, since: int, wait_ms: int = 20000) -> str:
        return self.run("serial", "--since", str(since), "--until", text, "--wait", str(wait_ms))


@pytest.fixture(scope="module")
def board(tmp_path_factory):
    b = Board(tmp_path_factory.mktemp("cyd"))
    b.run("flash", str(APP))
    try:
        yield b
    finally:
        b.run("stop", ok=False)


def read_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    w, h = struct.unpack(">II", data[16:24])
    idat = b""
    pos = 8
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        if tag == b"IDAT":
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3 + 1
    return w, h, b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(h))


def test_firmware_boots_through_the_real_rom_and_bootloader(board):
    log = board.expect('"t":"ready"', 0, 60000)
    assert "rst:0x1 (POWERON_RESET),boot:0x17 (SPI_FAST_FLASH_BOOT)" in log     # same banner as the hardware
    assert "entry 0x400805e4" in log
    assert '"fw":"hydro-deck-1.0"' in log


def test_screen_is_drawn_and_captured(board, tmp_path):
    out = tmp_path / "screen.png"
    board.run("screenshot", str(out), "--scale", "1", "--settle", "500")
    w, h, rgb = read_png(out)
    assert (w, h) == (320, 240)
    colours = {rgb[i:i + 3] for i in range(0, len(rgb), 3)}
    assert len(colours) > 20, "screen looks blank"
    st = board.status()
    assert st["display"]["on"] and not st["display"]["sleeping"]
    assert st["display"]["pixels_written"] >= 320 * 240
    assert st["backlight"]["brightness"] > 0


def test_adc_voltage_reaches_the_firmware(board):
    # the board profile puts 4120 mV on the battery divider; the firmware reports what it measured
    log = board.run("serial", "--all")
    assert '"vbat":4.12' in log


def test_factory_default_touch_calibration_lands_on_the_right_controls(board):
    # fresh chip, nothing in NVS: the firmware's built-in calibration must match the panel
    for x, expected in ((96, '"t":"scr","p":1'), (300, '"t":"scr","p":4'), (20, '"t":"scr","p":0')):
        off = board.serial_offset()
        board.run("tap", str(x), "224")                 # bottom nav: VITALS, SYS, DECK
        assert expected in board.expect('"t":"scr"', off)


def test_backlight_is_pwm_driven_and_sleep_turns_it_off(board):
    bl = board.status()["backlight"]
    assert bl["mode"] == "pwm", "backlight pin is not connected to its LEDC channel"
    assert 0.5 < bl["brightness"] <= 1.0

    board.run("send", '{"t":"sleep"}', "--wait", "300")
    board.run("wait", "1500")
    assert board.status()["backlight"]["brightness"] == 0

    board.run("tap", "300", "20")                       # any touch wakes it
    board.run("wait", "1500")
    assert board.status()["backlight"]["brightness"] > 0.5


def test_touch_is_read_through_the_xpt2046_and_calibration_persists(board):
    # run the firmware's own 4-point calibration against the virtual touch panel
    off = board.serial_offset()
    board.run("send", '{"t":"cal"}', "--wait", "500")
    for x, y in ((24, 24), (295, 24), (24, 215), (295, 215)):
        board.run("screenshot", "--settle", "300")
        board.run("tap", str(x), str(y), "--ms", "250")
        board.run("wait", "400")
    board.run("wait", "600")
    st = board.status()
    assert st["touch"]["transport"] == "bitbang" and st["touch"]["conversions"] > 0

    off = board.serial_offset()
    board.run("tap", "96", "224")                       # VITALS tab
    assert '"t":"scr","p":1' in board.expect('"t":"scr"', off)

    board.run("power", "cycle")                         # calibration lives in emulated NVS flash
    off = board.serial_offset()
    board.expect('"t":"ready"', off, 60000)
    board.run("screenshot", "--settle", "500")
    off = board.serial_offset()
    board.run("tap", "224", "224")                      # TIMER tab
    assert '"t":"scr","p":3' in board.expect('"t":"scr"', off)


def test_serial_input_and_boot_button(board, tmp_path):
    before = tmp_path / "before.png"
    after = tmp_path / "after.png"
    off = board.serial_offset()
    board.run("tap", "96", "224")                       # VITALS shows the telemetry
    board.expect('"t":"scr","p":1', off)
    board.run("screenshot", str(before), "--scale", "1", "--settle", "400")
    board.run("send", '{"t":"stat","host":"TEST","cpu":44,"gpu":73,"ram":52,"ruse":16.5,"rtot":32.0}', "--wait", "600")
    board.run("wait", "1500")                           # the firmware redraws telemetry on its own cadence
    board.run("screenshot", str(after), "--scale", "1", "--settle", "400")
    assert read_png(before)[2] != read_png(after)[2], "serial packet did not change the screen"

    off = board.serial_offset()
    board.run("button", "boot")
    assert '"t":"scr"' in board.expect('"t":"scr"', off)


def test_reset_button_reboots_the_firmware(board):
    off = board.serial_offset()
    board.run("reset")
    log = board.expect('"t":"ready"', off, 60000)
    assert "rst:" in log
