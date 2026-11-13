"""In-process binding: QEMU (the ESP32) + cyd_core (the board around it).

Only the worker process imports this. QEMU can be initialised once per process, so a new
flash image or a power cycle means a new worker.
"""
from __future__ import annotations

import ctypes as C
import os
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QEMU_DIR = ROOT / "vendor" / "qemu"
CORE_DLL = ROOT / "core" / "cyd_core.dll"

NGPIO = 40
EV_TOUCH, EV_PIN, EV_ADC, EV_RESET = 1, 2, 3, 4

LEDC_SIG_FIRST, LEDC_SIG_LAST = 71, 86      # LEDC_HS_SIG_OUT0 .. LEDC_LS_SIG_OUT7
SIG_GPIO_OUT = 0x100


class Config(C.Structure):
    _fields_ = [
        ("panel_w", C.c_int32), ("panel_h", C.c_int32), ("controller", C.c_int32),
        ("gram_mirror_x", C.c_int32), ("panel_bgr", C.c_int32), ("panel_inverted", C.c_int32),
        ("blank_white", C.c_int32),
        ("tft_dc", C.c_int32), ("tft_cs", C.c_int32), ("tft_rst", C.c_int32),
        ("touch_clk", C.c_int32), ("touch_mosi", C.c_int32), ("touch_miso", C.c_int32),
        ("touch_cs", C.c_int32), ("touch_irq", C.c_int32),
        ("reserved", C.c_int32 * 8),
    ]


class State(C.Structure):
    _fields_ = [
        ("virt_ns", C.c_int64), ("ticks", C.c_int64),
        ("pixels", C.c_int64), ("draw_seq", C.c_int64), ("tft_cmds", C.c_int64), ("tft_bytes", C.c_int64),
        ("last_draw_wall_ms", C.c_int64), ("last_draw_virt_ns", C.c_int64),
        ("spi_bytes", C.c_int64 * 2), ("spi_unrouted", C.c_int64),
        ("uart_tx_total", C.c_int64 * 3), ("uart_tx_dropped", C.c_int64 * 3),
        ("started", C.c_int32), ("exited", C.c_int32), ("resets", C.c_int32), ("events_pending", C.c_int32),
        ("view_w", C.c_int32), ("view_h", C.c_int32),
        ("madctl", C.c_uint8), ("view_madctl", C.c_uint8), ("colmod", C.c_uint8), ("inv", C.c_uint8),
        ("on", C.c_uint8), ("sleep", C.c_uint8), ("view_latched", C.c_uint8), ("brightness", C.c_uint8),
        ("x0", C.c_uint16), ("x1", C.c_uint16), ("y0", C.c_uint16), ("y1", C.c_uint16),
        ("tfa", C.c_uint16), ("vsa", C.c_uint16), ("bfa", C.c_uint16), ("vsp", C.c_uint16),
        ("touch_down", C.c_uint8), ("pad", C.c_uint8 * 3),
        ("touch_ypos", C.c_uint16), ("touch_xpos", C.c_uint16), ("touch_z1", C.c_uint16), ("touch_z2", C.c_uint16),
        ("touch_conversions", C.c_uint32), ("touch_bb_edges", C.c_uint32), ("touch_hw_bytes", C.c_uint32),
        ("out", C.c_uint8 * NGPIO), ("dir", C.c_uint8 * NGPIO), ("inp", C.c_uint8 * NGPIO),
        ("pin_writes", C.c_uint32 * NGPIO),
        ("out_sel", C.c_uint32 * NGPIO),
        ("in_sel_reserved", C.c_uint32),
        ("ledc_duty", C.c_float * 16),
        ("ledc_freq", C.c_uint32 * 8),
        ("ledc_conf0", C.c_uint32 * 16),
        ("uart_baud", C.c_uint32 * 3),
        ("strap", C.c_uint32),
    ]


class Machine:
    def __init__(self, board: dict, flash_path: Path, efuse_path: Path, icount_shift: int | None = 3,
                 wifi: bool = False, extra_args: list[str] | None = None):
        self.board = board
        self.pins = board["pins"]
        self.panel = board["panel"]
        os.add_dll_directory(str(QEMU_DIR))
        self._qemu = C.CDLL(str(QEMU_DIR / "libqemu-xtensa.dll"))
        self._core = C.CDLL(str(CORE_DLL))
        c = self._core
        c.cyd_error.restype = C.c_char_p
        c.cyd_virtual_ns.restype = C.c_int64
        c.cyd_render.argtypes = [C.c_char_p, C.c_int, C.c_int]
        c.cyd_get_state.argtypes = [C.POINTER(State)]
        c.cyd_view_to_panel.argtypes = [C.c_int, C.c_int, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int)]
        c.cyd_post_event.argtypes = [C.c_int] * 7
        c.cyd_uart_read.argtypes = [C.c_int, C.c_char_p, C.c_int]
        c.cyd_uart_write.argtypes = [C.c_char_p, C.c_int]
        if c.cyd_state_size() != C.sizeof(State) or c.cyd_config_size() != C.sizeof(Config):
            raise RuntimeError("cyd_core.dll does not match machine.py (rebuild with core\\build.bat)")

        self._rgb = C.create_string_buffer(320 * 480 * 3)
        self._uart_buf = C.create_string_buffer(1 << 16)

        cfg = Config()
        cfg.panel_w, cfg.panel_h = self.panel["w"], self.panel["h"]
        cfg.controller = 1 if self.panel["controller"] == "st7789" else 0
        cfg.gram_mirror_x = int(self.panel["gram_mirror_x"])
        cfg.panel_bgr = int(self.panel["bgr"])
        cfg.panel_inverted = int(self.panel["inverted"])
        cfg.blank_white = int(self.panel["blank_white"])
        for name in ("tft_dc", "tft_cs", "tft_rst", "touch_clk", "touch_mosi", "touch_miso", "touch_cs", "touch_irq"):
            setattr(cfg, name, int(self.pins.get(name, -1)))
        self._cfg = cfg

        defaults = (C.c_uint8 * NGPIO)()
        for gpio, level in board.get("input_defaults", {}).items():
            defaults[int(gpio)] = int(level)

        args = ["qemu-system-xtensa", "-M", "esp32-picsimlab",
                "-L", (QEMU_DIR / "fw").as_posix() + "/",
                "-drive", f"file={Path(flash_path).as_posix()},if=mtd,format=raw",
                "-drive", f"file={Path(efuse_path).as_posix()},if=none,format=raw,id=efuse",
                "-global", "driver=nvram.esp32.efuse,property=drive,value=efuse",
                "-serial", "none"]
        if board.get("strap") is not None:
            # levels the board's resistors put on the strapping pins at reset
            args += ["-global", f"driver=esp32.gpio,property=strap_mode,value={int(board['strap']):#x}"]
        if icount_shift is not None:
            args += ["-icount", f"shift={int(icount_shift)},align=off,sleep=on"]
        if wifi:
            args += ["-nic", "user,model=esp32_wifi"]
        args += list(extra_args or [])
        self.qemu_args = args
        self._argv = (C.c_char_p * (len(args) + 1))(*[a.encode() for a in args], None)
        rc = c.cyd_start(C.byref(cfg), defaults, len(args), self._argv)
        if rc != 0:
            raise RuntimeError(f"cyd_start failed: {c.cyd_error().decode()}")
        deadline = time.time() + 30
        while not c.cyd_started():
            if time.time() > deadline:
                raise RuntimeError("QEMU did not finish initialising")
            time.sleep(0.01)
        for adc in board.get("adc", {}).values():
            self.set_adc_mv(adc["gpio"], adc["default_mv"] / adc.get("divider", 1.0))

    # ------------------------------------------------------------ state
    def raw_state(self) -> State:
        s = State()
        self._core.cyd_get_state(C.byref(s))
        return s

    def _pin_drive(self, s: State, gpio: int) -> dict:
        """How the firmware is driving an output pin: plain GPIO level or LEDC PWM."""
        if gpio is None or gpio < 0:
            return {"mode": "none", "level": 0.0}
        sel = s.out_sel[gpio]
        func, inv = sel & 0x1FF, (sel >> 9) & 1
        if LEDC_SIG_FIRST <= func <= LEDC_SIG_LAST:
            ch = func - LEDC_SIG_FIRST
            conf = s.ledc_conf0[ch]
            timer = (conf & 3) + (4 if ch >= 8 else 0)
            duty = max(0.0, min(1.0, s.ledc_duty[ch] / 100.0))
            if not (conf >> 2) & 1:               # output disabled: idle level
                duty = float((conf >> 3) & 1)
            if inv:
                duty = 1.0 - duty
            return {"mode": "pwm", "level": round(duty, 4), "channel": ch, "freq_hz": int(s.ledc_freq[timer])}
        if func not in (SIG_GPIO_OUT, 0) and func < SIG_GPIO_OUT:
            # routed to another peripheral through the GPIO matrix (SPI, UART, ...)
            return {"mode": "periph", "level": 0.0, "signal": func}
        if not s.dir[gpio]:
            return {"mode": "input", "level": 0.0}
        level = float(s.out[gpio] ^ inv)
        return {"mode": "gpio", "level": level}

    def status(self) -> dict:
        s = self.raw_state()
        b = self.board
        bl = self._pin_drive(s, self.pins.get("backlight", -1))
        backlight = bl["level"] if b.get("backlight_active_high", True) else 1.0 - bl["level"]
        if bl["mode"] in ("input", "none"):
            backlight = 0.0
        out: dict = {
            "virtual_ms": s.virt_ns // 1_000_000,
            "exited": bool(s.exited),
            "resets": s.resets,
            "display": {
                "width": s.view_w, "height": s.view_h,
                "on": bool(s.on), "sleeping": bool(s.sleep), "inverted": bool(s.inv),
                "madctl": s.madctl, "view_madctl": s.view_madctl, "colmod": s.colmod,
                "initialised": bool(s.view_latched),
                "pixels_written": s.pixels, "draw_seq": s.draw_seq, "commands": s.tft_cmds,
                "idle_ms": s.last_draw_wall_ms,
                "idle_virtual_ms": (s.virt_ns - s.last_draw_virt_ns) // 1_000_000 if s.last_draw_virt_ns else -1,
                "window": [s.x0, s.y0, s.x1, s.y1],
                "scroll": s.vsp,
            },
            "backlight": {"brightness": round(backlight, 3), **{k: v for k, v in bl.items() if k != "level"}},
            "touch": {"down": bool(s.touch_down), "conversions": s.touch_conversions,
                      "transport": "bitbang" if s.touch_bb_edges else ("spi" if s.touch_hw_bytes else "unused")},
            "spi": {"hspi_bytes": s.spi_bytes[0], "vspi_bytes": s.spi_bytes[1], "unrouted_bytes": s.spi_unrouted},
            "uart": {"tx_bytes": s.uart_tx_total[0], "baud": s.uart_baud[0]},
        }
        leds = b.get("leds")
        if leds:
            rgb = {}
            for ch in ("r", "g", "b"):
                d = self._pin_drive(s, leds[ch])
                lvl = d["level"]
                if d["mode"] in ("input", "none"):
                    val = 0.0
                else:
                    val = 1.0 - lvl if leds.get("active_low", True) else lvl
                rgb[ch] = int(round(val * 255))
            out["led"] = {**rgb, "hex": "#%02x%02x%02x" % (rgb["r"], rgb["g"], rgb["b"])}
        audio = b.get("audio")
        if audio:
            d = self._pin_drive(s, audio["pin"])
            amp = None
            if "amp_enable" in audio:
                lvl = s.out[audio["amp_enable"]] if s.dir[audio["amp_enable"]] else 1
                amp = (lvl == 0) if audio.get("amp_active_low", True) else (lvl == 1)
            tone = d["mode"] == "pwm" and 0.0 < d["level"] < 1.0
            out["audio"] = {"tone_hz": d.get("freq_hz", 0) if tone else 0, "amp_enabled": amp}
        return out

    def gpio(self) -> list[dict]:
        s = self.raw_state()
        pins = []
        names = {v: k for k, v in self.pins.items() if isinstance(v, int) and v >= 0}
        leds = self.board.get("leds") or {}
        for ch in ("r", "g", "b"):
            if ch in leds:
                names[leds[ch]] = f"led_{ch}"
        audio = self.board.get("audio") or {}
        if "pin" in audio:
            names[audio["pin"]] = "speaker"
        if "amp_enable" in audio:
            names[audio["amp_enable"]] = "amp_enable"
        for name, spec in (self.board.get("adc") or {}).items():
            names[spec["gpio"]] = name
        for g in range(NGPIO):
            d = self._pin_drive(s, g)
            pins.append({"gpio": g, "name": names.get(g, ""), "mode": d["mode"], "level": d["level"],
                         "input_level": s.inp[g], "writes": s.pin_writes[g],
                         **({"freq_hz": d["freq_hz"]} if "freq_hz" in d else {}),
                         **({"signal": d["signal"]} if "signal" in d else {})})
        return pins

    # ------------------------------------------------------------ display
    def frame(self, rotation: int = -1, backlight: str = "gate") -> tuple[int, int, bytes]:
        level = 255
        if backlight != "ignore":
            bright = self.status()["backlight"]["brightness"]
            if backlight == "scale":
                level = int(round(bright * 255))
            else:
                level = 0 if bright <= 0.0 else 255
        wh = self._core.cyd_render(self._rgb, rotation, level)
        w, h = wh >> 16, wh & 0xFFFF
        return w, h, self._rgb.raw[: w * h * 3]

    # ------------------------------------------------------------ input
    def _raw_touch(self, x: int, y: int, rotation: int = -1) -> tuple[int, int]:
        s = self.raw_state()
        if rotation < 0:
            w, h = s.view_w, s.view_h
        else:
            w, h = (self.panel["h"], self.panel["w"]) if rotation & 1 else (self.panel["w"], self.panel["h"])
        x = max(0, min(w - 1, int(x)))
        y = max(0, min(h - 1, int(y)))
        u, v = C.c_int(), C.c_int()
        self._core.cyd_view_to_panel(rotation, x, y, C.byref(u), C.byref(v))
        t = self.board["touch"]
        vals = {}
        for ch in ("ch90", "chD0"):
            spec = t[ch]
            pos, span = (u.value, self.panel["w"] - 1) if spec["axis"] == "u" else (v.value, self.panel["h"] - 1)
            vals[ch] = int(round(spec["at0"] + (spec["at_max"] - spec["at0"]) * pos / span))
        return max(0, min(4095, vals["ch90"])), max(0, min(4095, vals["chD0"]))

    def touch_event(self, delay_ms: int, down: bool, x: int = 0, y: int = 0, rotation: int = -1) -> None:
        t = self.board["touch"]
        if down:
            ch90, chd0 = self._raw_touch(x, y, rotation)
            self._core.cyd_post_event(int(delay_ms), EV_TOUCH, 1, ch90, chd0, int(t["z1"]), int(t["z2"]))
        else:
            self._core.cyd_post_event(int(delay_ms), EV_TOUCH, 0, 0, 0, 0, 0)

    def touch_raw_event(self, delay_ms: int, down: bool, ch90: int, chd0: int) -> None:
        t = self.board["touch"]
        self._core.cyd_post_event(int(delay_ms), EV_TOUCH, int(bool(down)), int(ch90), int(chd0), int(t["z1"]), int(t["z2"]))

    def pin_event(self, delay_ms: int, gpio: int, level: int) -> None:
        self._core.cyd_post_event(int(delay_ms), EV_PIN, int(gpio), int(bool(level)), 0, 0, 0)

    def set_adc_mv(self, gpio: int, millivolts: float) -> None:
        """Put a voltage on an ADC1 pin (GPIO32-39).

        Inverse of the ESP32's default 11 dB calibration curve (no eFuse Vref burned):
        mV = raw * 1100 * 3.0 / 4096 + 142, which is what analogReadMilliVolts() applies.
        """
        channel = {36: 0, 37: 1, 38: 2, 39: 3, 32: 4, 33: 5, 34: 6, 35: 7}.get(int(gpio))
        if channel is None:
            raise ValueError(f"GPIO{gpio} is not an ADC1 pin")
        raw = max(0, min(4095, int(round((millivolts - 142.0) / (1100.0 * 3.0 / 4096.0)))))
        self._core.cyd_post_event(0, EV_ADC, channel, raw, 0, 0, 0)

    def reset(self) -> None:
        self._core.cyd_post_event(0, EV_RESET, 0, 0, 0, 0, 0)

    def events_pending(self) -> int:
        return self._core.cyd_events_pending()

    def virtual_ms(self) -> int:
        return self._core.cyd_virtual_ns() // 1_000_000

    # ------------------------------------------------------------ serial
    def uart_read(self, uart: int = 0) -> bytes:
        chunks = []
        while True:
            n = self._core.cyd_uart_read(uart, self._uart_buf, len(self._uart_buf))
            if n <= 0:
                break
            chunks.append(self._uart_buf.raw[:n])
            if n < len(self._uart_buf):
                break
        return b"".join(chunks)

    def uart_write(self, data: bytes) -> int:
        return self._core.cyd_uart_write(data, len(data))
