HYDRO-DECK FLASH GUIDE (2.8" CYD: E32R28T / ESP32-2432S028R)
=============================================================
Images in this folder:
  HYDRO-DECK_E32R28T_ILI9341_firmware.bin   ILI9341 panels (most boards)
  HYDRO-DECK_E32R28T_ST7789_firmware.bin    ST7789 panels
  HYDRO-DECK_E32R28T_<panel>_MERGED.bin     single file for web flashers

1. USB with esptool or PlatformIO (four files):
     0x1000   <variant>_bootloader.bin
     0x8000   <variant>_partitions.bin
     0xe000   boot_app0.bin
     0x10000  <variant>_firmware.bin
   In PC Studio click 'Release COM Port' first so the port is free.

2. Web flasher (https://esp.huhn.me or similar):
   flash <variant>_MERGED.bin at address 0x0.

3. If esptool reports 'no sync reply' or 'No serial data received':
   - Some boards only enter download mode by hand: hold BOOT while plugging USB in,
     then flash straight away.
   - Unplug anything on the rear 4-pin UART connector (P2). Its RX pin is GPIO3 (RXD0),
     the same net as the CH340's TX (through a 100 ohm resistor), so a GPS or other
     module there can block PC -> board data.

4. Do NOT install this through another firmware's microSD "update firmware" screen
   (HaleHound / WontHound and similar). The partition table has a single app slot, so
   such an updater would have to overwrite the firmware it is running from. ESP-IDF
   refuses that write and the board just reboots; if it did not refuse, an interrupted
   write would leave the board unbootable.
