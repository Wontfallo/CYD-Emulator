#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "protocol.h"

enum class TouchEventType : uint8_t {
    NONE = 0,
    PRESS,
    DRAG_MOVE,
    RELEASE,
    TAP,
    LONG_PRESS,
    SWIPE_LEFT,
    SWIPE_RIGHT,
    SWIPE_UP,
    SWIPE_DOWN
};

struct TouchEvent {
    TouchEventType type = TouchEventType::NONE;
    int16_t x = 0;
    int16_t y = 0;
    int16_t startX = 0;
    int16_t startY = 0;
    uint16_t holdMs = 0;
    bool isDown = false;
};

class TouchInput {
public:
    void begin(const TouchCalData& cal, uint8_t rotation);
    void setCalibration(const TouchCalData& cal);
    void setRotation(uint8_t rotation);
    const TouchCalData& getCalibration() const { return _cal; }

    bool readRawPoint(uint16_t& rawX, uint16_t& rawY, uint16_t& rawZ);
    bool readScreenPoint(int16_t& sx, int16_t& sy);
    TouchEvent poll();

    // Non-blocking 4-point calibration wizard FSM (plus blocking compatibility wrapper)
    void startCalibration(TFT_eSPI& tft);
    bool isCalibrating() const { return _calActive; }
    void cancelCalibration() {
        _calActive = false;
        _calWaitRelease = false;
        _calSamples = 0;
        _calSumX = 0;
        _calSumY = 0;
    }
    bool stepCalibration(TFT_eSPI& tft, TouchCalData& outCal, bool& finished, bool& ok);
    bool runCalibrationWizard(TFT_eSPI& tft, TouchCalData& outCal, void (*idleCb)() = nullptr);

private:
    TouchCalData _cal{};
    uint8_t _rotation = 1;

    bool _down = false;
    bool _longFired = false;
    bool _dragged = false;
    int16_t _startX = 0, _startY = 0;
    int16_t _lastX = 0, _lastY = 0;
    uint32_t _downMs = 0;
    uint32_t _lastReleaseMs = 0;
    uint32_t _lastHoldEmitMs = 0;

    bool _calActive = false;
    bool _calWaitRelease = false;
    uint8_t _calIdx = 0;
    uint8_t _calSamples = 0;
    uint32_t _calSumX = 0;
    uint32_t _calSumY = 0;
    uint32_t _calStageMs = 0;
    uint16_t _calRxs[4]{};
    uint16_t _calRys[4]{};

    uint16_t readXptChannel(uint8_t cmd);
    void mapRawToScreen(uint16_t rx, uint16_t ry, int16_t& sx, int16_t& sy) const;
    void drawCalibrationTarget(TFT_eSPI& tft, uint8_t idx);
};

