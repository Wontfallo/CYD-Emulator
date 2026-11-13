# streamdeck / CYD workspace

## Testing firmware: use the virtual CYD

`cyd-emu/` is an emulator of the CYD board that runs real ESP32 firmware `.bin` files
(see [cyd-emu/README.md](cyd-emu/README.md)). When you build or change firmware for the CYD,
verify it there by looking at the screen — do not rely on "it compiles".

```
cyd-emu\cydemu.cmd flash firmware\.pio\build\e32r28t_ili9341     # or any app/merged .bin
cyd-emu\cydemu.cmd screenshot                                     # prints a PNG path: read it
cyd-emu\cydemu.cmd tap 96 224                                     # screen pixels (PNG px / 2)
cyd-emu\cydemu.cmd serial                                         # firmware log
cyd-emu\cydemu.cmd status                                         # backlight, LED, display, touch
```

Screenshots are scaled x2; touch coordinates are unscaled screen pixels. Re-flashing keeps
NVS/filesystem (add `--erase` for a clean chip). Board profile defaults to `e32r28t`
(ILI9341); use `--board e32r28t-st7789` for ST7789 builds.

## Layout

- `firmware/` — HYDRO-DECK firmware (PlatformIO, envs `e32r28t_ili9341`, `e32r28t_st7789`)
- `firmware_dist/` — built images
- `companion/` — PC companion app. `companion/virtual_cyd/` is an older source-level
  simulator (firmware recompiled against shims); `cyd-emu/` supersedes it for testing.
- `cyd-emu/` — the virtual CYD

## Real hardware

Two physical boards have been used with this project (both show up as CH340 on COM11 when
plugged into the same USB port):

- **Original CYD (ESP32-2432S028R):** flashes normally with esptool's automatic reset. It runs
  HYDRO-DECK as of 2026-11-12.
- **"3E" (Freenove ESP32-32E / E32R28T with NRF24 + GPS wired on):** needs manual download
  mode - hold BOOT while plugging USB in, then run the flash command as the first thing that
  touches the port. Do not reset it or open the port beforehand. Its notes are in the owner's
  WontHound-CYD tree (`_boards_separated/CYD-ESP32-3E/README.md`). Command-line flashing of
  HYDRO-DECK to it has not succeeded yet; `firmware_dist/*_MERGED.bin` (flash at 0x0) exist
  for use with a web flasher.
