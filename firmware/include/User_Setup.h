#pragma once

#define DISABLE_ALL_LIBRARY_WARNINGS
#define USER_SETUP_INFO "HYDRO-DECK-E32R28T"

#if defined(HYDRO_PANEL_ST7789)
  #define ST7789_DRIVER
  #define TFT_RGB_ORDER TFT_RGB
  #define TFT_INVERSION_OFF
  #define SPI_FREQUENCY 40000000
#else
  // Default: E32R28T 2.8" ILI9341 (matches FNK0103F + WontHound-CYD)
  #define ILI9341_2_DRIVER
  #define TFT_RGB_ORDER TFT_BGR
  #define TFT_INVERSION_OFF
  #define SPI_FREQUENCY 40000000
#endif

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// TFT_BL is deliberately NOT defined. If it is, TFT_eSPI::init() does
// pinMode(TFT_BL, OUTPUT) + digitalWrite(HIGH), which reconnects GPIO21 to the plain GPIO
// output and silently detaches the LEDC channel HwFeedback::begin() attached to it: the
// backlight then stays at 100% and brightness / sleep fade never reach the pin.
// HwFeedback owns the backlight (PIN_TFT_BL in board_config.h).

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1

#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY  2500000
#define USE_HSPI_PORT

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
