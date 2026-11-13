HYDRO-DECK SD / USB FLASH GUIDE (2.8" E32R28T)
=================================================
Panel Variants Included:
  - HYDRO-DECK_E32R28T_ILI9341_firmware.bin (most standard 2.8" E32R28T ILI9341 panels)
  - HYDRO-DECK_E32R28T_ST7789_firmware.bin  (ST7789VIPS revision panels)

1. If COM11 USB-Serial TX fails with 'No serial data received':
   Unplug any external module from the back 4-pin UART connector (P2: 5V/GND/TXD/RXD).
   P2 shares GPIO3 (RXD0) directly with the CH340 USB-UART chip (R29/R30).
   In PC Studio click 'Release COM Port' first, then flash via USB.

2. Alternative via HaleHound-CYD microSD Firmware Updater:
   Copy the matching variant bin (HYDRO-DECK_E32R28T_ILI9341_firmware.bin OR
   HYDRO-DECK_E32R28T_ST7789_firmware.bin) to microSD as firmware.bin and flash.

3. Direct esptool full-image offsets (when flashing blank ESP32 over USB):
   0x1000  <variant>_bootloader.bin
   0x8000  <variant>_partitions.bin
   0xe000  boot_app0.bin
   0x10000 <variant>_firmware.bin
