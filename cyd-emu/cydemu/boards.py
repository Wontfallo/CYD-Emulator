"""Board profiles: what is soldered to which ESP32 pin on each CYD variant.

A profile describes hardware only. It never describes a firmware.

Touch model
-----------
`touch` gives the raw 12-bit XPT2046 reading at each edge of the panel, in the viewer's
portrait frame (u: 0 = left .. 239 = right, v: 0 = top .. 319 = bottom, USB connector side
is the bottom on a 2.8" CYD held portrait).  `ch90` is the channel selected by control byte
0x9x (the datasheet's "Y position"), `chD0` the one selected by 0xDx ("X position").
The numbers are the ones the CYD community calibrates to for landscape use
(x: 200..3700 across the long side, y: 240..3800 across the short side).
"""
from __future__ import annotations

import copy

_CYD_COMMON = {
    "flash_size": 4 * 1024 * 1024,
    "panel": {
        "w": 240, "h": 320,
        "controller": "ili9341",
        "gram_mirror_x": 1,     # ILI9341 modules need MADCTL.MX for an upright portrait image
        "bgr": 1,               # BGR colour filter: firmware must set MADCTL.BGR
        "inverted": 0,
        "blank_white": 1,       # TN panel with the backlight on shows white until driven
    },
    "pins": {
        "tft_dc": 2, "tft_cs": 15, "tft_rst": -1, "backlight": 21,
        "touch_clk": 25, "touch_mosi": 32, "touch_miso": 39, "touch_cs": 33, "touch_irq": 36,
        "boot": 0,
        "sd_cs": 5,
    },
    "backlight_active_high": True,
    # GPIO_STRAP as the real board reports it at power-on ("boot:0x17" in the ROM banner):
    # GPIO0, GPIO4, GPIO15 and GPIO5 high, GPIO2 low.
    "strap": 0x17,
    "touch": {
        "ch90": {"axis": "v", "at0": 200, "at_max": 3700},
        "chD0": {"axis": "u", "at0": 3800, "at_max": 240},
        "z1": 1100, "z2": 2900,
    },
    # levels the board itself puts on input pins (pull-ups, idle lines)
    "input_defaults": {0: 1, 35: 1, 36: 1, 3: 1},
}


def _derive(base: dict, **over) -> dict:
    out = copy.deepcopy(base)
    for key, val in over.items():
        if isinstance(val, dict) and isinstance(out.get(key), dict):
            out[key].update(val)
        else:
            out[key] = val
    return out


BOARDS: dict[str, dict] = {
    # LCDWiki / Freenove 2.8" ESP32-32E "E32R28T": RGB LED on 22/16/17, amp enable on 4,
    # speaker on 26 (DAC2), battery divider on 34.
    # Touch ranges are the vendor's own calibration line for this board
    # (Freenove Sketch_12.1: touch.setCal(493, 3398, 706, 3446, 320, 240, 1) at rotation 1).
    "e32r28t": _derive(
        _CYD_COMMON,
        description='2.8" ESP32-32E E32R28T, ILI9341 240x320, XPT2046 resistive touch',
        touch={"ch90": {"axis": "v", "at0": 493, "at_max": 3398},
               "chD0": {"axis": "u", "at0": 3446, "at_max": 706}},
        leds={"r": 22, "g": 16, "b": 17, "active_low": True},
        audio={"pin": 26, "amp_enable": 4, "amp_active_low": True},
        adc={"battery": {"gpio": 34, "channel": 6, "divider": 2.0, "default_mv": 4120}},
    ),
    "e32r28t-st7789": _derive(
        _CYD_COMMON,
        description='2.8" ESP32-32E E32R28T-1, ST7789 240x320, XPT2046 resistive touch',
        touch={"ch90": {"axis": "v", "at0": 493, "at_max": 3398},
               "chD0": {"axis": "u", "at0": 3446, "at_max": 706}},
        panel={"controller": "st7789", "gram_mirror_x": 0, "bgr": 0, "inverted": 0, "blank_white": 0},
        leds={"r": 22, "g": 16, "b": 17, "active_low": True},
        audio={"pin": 26, "amp_enable": 4, "amp_active_low": True},
        adc={"battery": {"gpio": 34, "channel": 6, "divider": 2.0, "default_mv": 4120}},
    ),
    # The original "Cheap Yellow Display": RGB LED on 4/16/17, LDR on 34, speaker on 26.
    "esp32-2432s028r": _derive(
        _CYD_COMMON,
        description='ESP32-2432S028R classic CYD, ILI9341 240x320, XPT2046 resistive touch',
        leds={"r": 4, "g": 16, "b": 17, "active_low": True},
        audio={"pin": 26},
        adc={"ldr": {"gpio": 34, "channel": 6, "divider": 1.0, "default_mv": 600}},
    ),
    # Two-USB revision of the classic board: ST7789 panel, colours inverted unless INVON is sent.
    "esp32-2432s028r-st7789": _derive(
        _CYD_COMMON,
        description='ESP32-2432S028R "CYD2USB", ST7789 240x320, XPT2046 resistive touch',
        panel={"controller": "st7789", "gram_mirror_x": 0, "bgr": 1, "inverted": 0, "blank_white": 0},
        leds={"r": 4, "g": 16, "b": 17, "active_low": True},
        audio={"pin": 26},
        adc={"ldr": {"gpio": 34, "channel": 6, "divider": 1.0, "default_mv": 600}},
    ),
}

DEFAULT_BOARD = "e32r28t"


def get_board(name: str) -> dict:
    key = (name or DEFAULT_BOARD).lower()
    if key not in BOARDS:
        raise KeyError(f"unknown board '{name}'. Known boards: {', '.join(sorted(BOARDS))}")
    board = copy.deepcopy(BOARDS[key])
    board["name"] = key
    return board
