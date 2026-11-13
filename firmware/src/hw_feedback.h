#pragma once
#include <Arduino.h>
#include "protocol.h"

enum class SfxType : uint8_t {
    SFX_TAP = 0,
    SFX_TOGGLE_ON,
    SFX_TOGGLE_OFF,
    SFX_PAGE,
    SFX_ERROR,
    SFX_BOOT,
    SFX_ALARM
};

enum class BootBtnAction : uint8_t {
    NONE = 0,
    SHORT_PRESS,
    LONG_PRESS
};

class HwFeedback {
public:
    void begin(const DeviceSettings& cfg);
    void loop(float maxLoadPct, bool isHot, bool sleeping);

    void applySettings(const DeviceSettings& cfg);
    void setBrightnessImmediate(uint8_t val);
    void notifyActivity();
    bool isSleeping() const { return _sleeping; }
    void wake();
    void sleep();

    void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs = 180);
    void playSfx(SfxType sfx);
    BootBtnAction pollBootButton();
    void clearBootButton();
    float readBatteryVolts();

private:
    DeviceSettings _cfg{};
    uint8_t _curBl = 220;
    uint8_t _targetBl = 220;
    uint32_t _lastActivityMs = 0;
    uint32_t _lastBlTickMs = 0;
    bool _sleeping = false;

    uint32_t _pulseUntilMs = 0;
    uint8_t _pulseR = 0, _pulseG = 255, _pulseB = 255;

    bool _bootLast = true;
    uint32_t _bootDownMs = 0;
    bool _bootLongFired = false;

    void writeRgb(uint8_t r, uint8_t g, uint8_t b);
    static void audioTask(void* param);
};
