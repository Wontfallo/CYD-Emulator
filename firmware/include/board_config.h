#pragma once

// Verified hardware pinout for 2.8" CYD E32R28T / E32R28T-1 (ESP32-WROOM-32E)
// Sources: Freenove_ESP32_Display-main schematic + WontHound-CYD cyd_config.h

#define CYD_SCREEN_W 320
#define CYD_SCREEN_H 240

// Display SPI (HSPI)
#define PIN_TFT_MISO 12
#define PIN_TFT_MOSI 13
#define PIN_TFT_SCLK 14
#define PIN_TFT_CS   15
#define PIN_TFT_DC   2
#define PIN_TFT_RST  -1
#define PIN_TFT_BL   21

// Resistive Touch XPT2046 (dedicated bit-banged SPI bus — avoids VSPI/HSPI conflicts)
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_IRQ  36

// RGB Status LED (LED2 - common anode, active LOW)
#define PIN_LED_R 22
#define PIN_LED_G 16
#define PIN_LED_B 17

// Audio Amplifier FM8002E / SC8002B
#define PIN_AUDIO_EN  4   // LOW = active, HIGH = shutdown
#define PIN_AUDIO_DAC 26  // ESP32 DAC2

// Physical BOOT Button (KEY2)
#define PIN_BOOT_BTN 0

// Battery Voltage Divider ADC (2x 100k -> multiply mV by 2)
#define PIN_BAT_ADC 34

// LEDC PWM channels (Arduino ESP32 core 2.x)
#define PWM_CH_BL     0
#define PWM_CH_LEDR   1
#define PWM_CH_LEDG   2
#define PWM_CH_LEDB   3
#define PWM_CH_AUDIO  4
#define PWM_FREQ_BL   5000
#define PWM_FREQ_LED  5000
#define PWM_RES_8BIT  8
