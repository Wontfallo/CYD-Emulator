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

A physical CYD is on COM11. As of 2026-11-12 its PC→board serial line does not work
(esptool: "serial TX path seems to be down"), so it cannot be flashed over USB, and it is
running HaleHound-CYD v3.9.3, not HYDRO-DECK.
