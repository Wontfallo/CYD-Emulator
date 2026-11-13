# cydemu — a virtual Cheap Yellow Display

A CYD you can flash. It takes the same `.bin` you would write to the real board with
esptool, runs it on an emulated ESP32, and lets you (or an AI agent) look at the screen,
touch it, press BOOT, talk to it over serial and read its pins.

Nothing is recompiled and nothing is stubbed at the source level. The firmware's own machine
code runs: mask ROM → second-stage bootloader → your app, FreeRTOS on both cores, the display
driver writing real SPI registers. Tested so far with TFT_eSPI-based firmware (HYDRO-DECK and
Freenove's prebuilt NerdMiner image); other display libraries have not been tried yet.

```
cydemu flash  firmware.bin        # write it to the virtual flash chip and boot
cydemu screenshot                 # -> prints a PNG path; open it and look
cydemu tap 96 224                 # touch the screen (screen pixels)
cydemu serial                     # what the firmware printed
```

Windows 10/11, 64-bit only (the emulator core is a Windows DLL).

## Install

**Portable (no Python needed):** download `cydemu-portable-win64-v*.zip` from the repository's
Releases page, unzip it anywhere, then either

- drag a firmware `.bin` onto **Flash firmware (drop a .bin here).cmd** — it flashes, boots and
  opens the live view in your browser, or
- double-click **Flash demo firmware.cmd** to try the bundled HYDRO-DECK firmware, or
- use `cydemu.cmd <command>` from a terminal in that folder.

**From source:** clone the repository and run `cyd-emu\cydemu.cmd`, or `python -m cydemu` with
this folder on `PYTHONPATH`. Python 3.10+, no pip packages needed. `python tools\make_portable.py`
rebuilds the portable zip.

Each virtual board keeps its flash chip in `state/<instance>/` next to the program, so a
portable folder is self-contained: delete the folder and nothing is left behind.

## For AI agents: the edit → flash → look loop

1. Build the firmware as usual (`pio run`, Arduino CLI, `idf.py build`).
2. `cydemu flash <path>` — an app `.bin`, a merged image, or the build directory
   (e.g. `firmware/.pio/build/e32r28t_ili9341`). Bootloader and partition table sitting next
   to the app are picked up automatically. The board reboots into the new firmware.
3. `cydemu screenshot` — waits until the firmware has stopped drawing, then prints the PNG
   path. Read that file to see the screen. The image is scaled x2; **touch coordinates are
   screen pixels**, i.e. image pixels divided by the scale the command printed.
4. `cydemu tap X Y`, `hold X Y`, `swipe X1 Y1 X2 Y2`, `button boot` — then screenshot again.
5. `cydemu serial` for logs, `cydemu send '<text>' --expect '<reply>'` to talk to it,
   `cydemu status` for backlight / RGB LED / audio / display state, `cydemu gpio` for pins.

Things worth knowing:

- Re-flashing keeps NVS and the filesystem, like real hardware. Use `flash --erase` for a
  factory-fresh chip.
- Time inside the emulator is *device time*. `cydemu wait 500` lets 500 ms of it pass.
  Touch gestures and `--settle` are measured in device time, so results do not depend on how
  fast the PC is.
- Every command takes `--json`.
- If the screenshot is black or white, read the NOTE lines the command prints: they say
  whether the firmware never drew, the display is asleep, or the backlight is off. A blank
  panel is often the truth — it is what the real board would show.
- `cydemu view` prints the URL of a live page (screen, clickable touch, serial console).

## Commands

| Command | What it does |
| --- | --- |
| `start [--board B] [--port N] [--no-wifi] [--icount N]` | Start the daemon. Other commands start it on demand. |
| `stop` | Power off and stop the daemon. |
| `status` | Firmware, power, display, backlight, RGB LED, audio, touch, serial counters. |
| `flash PATH [--erase] [--board B] [--bootloader F] [--partitions F.bin\|F.csv] [--offset 0x..] [--no-boot]` | Write to flash and boot. |
| `erase` | Erase the whole chip (board powers off; it cannot boot until flashed). |
| `reset` | Pulse EN (the reset button). |
| `power on\|off\|cycle` | Power switch. |
| `screenshot [OUT.png] [--scale 2] [--settle 300] [--timeout 15000] [--rotation auto\|0..3] [--backlight gate\|scale\|ignore]` | Save the screen. |
| `tap X Y [--ms 100]`, `hold X Y [--ms 800]`, `swipe X Y X2 Y2 [--ms 300]` | Touch gestures. |
| `touch down X Y` / `touch move X Y` / `touch up` | Manual touch control. |
| `button [boot] [--ms 150] [--action click\|press\|release]` | Hardware button on GPIO0. |
| `serial [--tail N \| --since OFFSET \| --all] [--wait MS] [--until TEXT] [--follow]` | Read UART0 output. |
| `send TEXT [--no-newline] [--expect TEXT] [--wait MS]` | Write to UART0, print the reply. |
| `adc battery 3700` / `adc 34 1850` | Put a voltage (mV) on an analog input. |
| `gpio [--set GPIO LEVEL]` | Show every pin the firmware drives, or drive an input pin. |
| `wait MS` | Let device time pass. |
| `boards` | List board profiles. |
| `view` | URL of the live viewer. |

`--instance NAME` selects a separate virtual board (own flash chip, own port) so several can
run at once. State lives in `state/<instance>/`.

## HTTP API

The CLI is a thin client; anything can drive the daemon directly at `http://127.0.0.1:8790`.

| Request | |
| --- | --- |
| `GET /status`, `GET /gpio`, `GET /boards` | JSON state |
| `GET /screenshot.png?scale=&settle_ms=&timeout_ms=&rotation=&backlight=` | PNG |
| `GET /serial?since=N` \| `?tail=N`, `&wait_ms=`, `&until=` | `{offset, next, end, data}` |
| `POST /serial` | raw body, or `{"text": "...", "newline": true}` |
| `POST /flash` | `{"path", "erase", "bootloader", "partitions", "offset", "board", "boot"}` |
| `POST /touch` | `{"action": "tap\|hold\|swipe\|down\|move\|up", "x", "y", "x2", "y2", "duration_ms"}` |
| `POST /button` | `{"name": "boot", "action": "click\|press\|release", "duration_ms"}` |
| `POST /adc` | `{"name": "battery", "mv": 3700}` or `{"gpio": 34, "mv": 1850}` |
| `POST /gpio` | `{"gpio": 35, "level": 0}` |
| `POST /reset`, `/erase`, `/power` `{"on": bool}` \| `{"cycle": true}`, `/wait` `{"virtual_ms"}`, `/shutdown` | |

## Boards

| Profile | Hardware |
| --- | --- |
| `e32r28t` (default) | 2.8" ESP32-32E E32R28T / Freenove FNK0103F — ILI9341, XPT2046, RGB LED 22/16/17, amp enable 4, battery ADC 34 |
| `e32r28t-st7789` | Same board with the ST7789 panel |
| `esp32-2432s028r` | Classic CYD — ILI9341, XPT2046, RGB LED 4/16/17, LDR on 34 |
| `esp32-2432s028r-st7789` | Two-USB classic CYD with ST7789 |

Profiles are plain data in `cydemu/boards.py`: pins, panel type, colour order, touch ranges.
Flashing the wrong build for a panel gives the same wrong colours / mirrored image the real
board would.

## What is emulated

| Part | How |
| --- | --- |
| ESP32 CPU, ROM, flash, eFuse, timers, UART, SPI, I2C, LEDC | QEMU (`vendor/qemu`, PICSimLab's ESP32 build). 4 MB flash image on disk, strapping pins as on the board (`boot:0x17`). |
| TFT (ILI9341 / ST7789) | MIPI-DCS decoder fed by every byte the firmware clocks out on HSPI/VSPI: window, MADCTL rotation, RGB/BGR, 16/18-bit colour, inversion, sleep, display on/off, scrolling, ID and pixel read-back. An unwritten GRAM shows noise; an undriven TN panel shows white. |
| Touch (XPT2046) | Bit-accurate serial model. Works whether the firmware bit-bangs GPIO 25/32/39/33 or uses a hardware SPI bus. PENIRQ on GPIO36. |
| Backlight, RGB LED, speaker | Read from what actually drives the pin — plain GPIO level or LEDC PWM duty and frequency through the GPIO matrix. |
| BOOT button, analog inputs | GPIO0; ADC1 voltages using the ESP32's default calibration curve. |
| USB serial | UART0 both ways, input paced at the firmware's baud rate. |
| Wi-Fi | QEMU's ESP32 Wi-Fi model (on by default; `start --no-wifi` to leave it out). Enough for firmware to bring up its access point instead of crashing. |

## Limits — where this is not yet 1:1

- **Touch calibration numbers are unverified on your board.** The `e32r28t` raw ranges come
  from the vendor's example sketch, not from a measurement of a physical unit. Firmware with a
  calibration routine is unaffected; firmware with hard-coded calibration constants will only
  line up if those constants match.
- **No SD card.** The slot reads as empty.
- **No Bluetooth, no DAC audio.** Tones generated with LEDC PWM are reported; `dacWrite` is not.
- **Wi-Fi** has only been tested as far as "AP mode starts". No port forwarding from the PC to
  the device is set up.
- **Speed.** Roughly real time when the firmware is idle, slower while it is busy. Device time
  stays self-consistent (delays, timeouts and watchdogs are correct); wall-clock time is not.
- **Only ESP32 (WROOM-32) boards with SPI panels and XPT2046.** Capacitive-touch (GT911,
  CST816, FT6336) and ESP32-S3 CYD variants are not modelled.
- Display tearing, SPI clock speed and panel response time are not modelled; the image is
  what is in the controller's memory.

## Layout

```
cydemu.cmd            launcher
cydemu/               Python: CLI, daemon (HTTP API + viewer), worker, flash chip, board profiles
core/cyd_core.c       the board's peripherals, in C; build with core\build.bat (MSVC)
vendor/qemu/          QEMU ESP32 library + ROM images (see SOURCE.txt; GPL v2)
defaults/             stock Arduino-ESP32 bootloader + partition table for app-only .bin files
state/<instance>/     flash.bin, efuse.bin, logs, screenshots
tests/                pytest: flash chip rules + end-to-end run of a real firmware
tools/                make_portable.py: builds the no-install download
```

One daemon per virtual board. It keeps the flash chip and serial log, and spawns a worker
process that hosts QEMU for as long as the board is powered; power-cycling or re-flashing
replaces the worker.

Run the tests with `python -m pytest tests` from this folder (about a minute).

## Using it with the HYDRO-DECK companion app

The companion (`../companion`) looks for a running virtual CYD and treats it like a board on
USB: telemetry and deck layout go in over the emulated serial port, taps come back out and run
your macros on the PC. Start the emulator with the HYDRO-DECK firmware, then start the
companion; its state reports the link under `cydemu`. Set `CYDEMU_URL` to point it at a
non-default instance, or `HYDRO_SKIP_CYDEMU=1` to keep it away.

## License

cydemu's own code (`cydemu/`, `core/`, `tools/`, `tests/`) is GPL-2.0-or-later, because it loads
and extends QEMU in-process. See `LICENSE`.

Bundled third-party software: QEMU for ESP32 from the PICSimLab project (GPL v2; source and
exact origin in `vendor/qemu/SOURCE.txt`) with its runtime libraries, the ESP32 mask ROM images
QEMU boots from, and the Arduino-ESP32 bootloader in `defaults/`. The portable build also carries
a CPython runtime under the PSF license (`python/LICENSE.txt`).
