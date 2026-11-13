#include "touch_input.h"
#include "board_config.h"
#include "ui_theme.h"

void TouchInput::begin(const TouchCalData& cal, uint8_t rotation) {
    _cal = cal;
    _rotation = rotation;
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    pinMode(PIN_TOUCH_CLK, OUTPUT);
    digitalWrite(PIN_TOUCH_CLK, LOW);
    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    digitalWrite(PIN_TOUCH_MOSI, LOW);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT);
}

void TouchInput::setCalibration(const TouchCalData& cal) {
    _cal = cal;
}

void TouchInput::setRotation(uint8_t rotation) {
    _rotation = rotation;
}

uint16_t TouchInput::readXptChannel(uint8_t cmd) {
    uint16_t res = 0;
    digitalWrite(PIN_TOUCH_CLK, LOW);
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(PIN_TOUCH_MOSI, (cmd >> i) & 0x01);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_CLK, LOW);
    }
    digitalWrite(PIN_TOUCH_MOSI, LOW);
    delayMicroseconds(2);
    // Clock 9: XPT2046 BUSY bit (0) before DB11 appears on falling edge of clock 9
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_TOUCH_CLK, LOW);
    delayMicroseconds(1);
    for (int8_t i = 11; i >= 0; i--) {
        digitalWrite(PIN_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        if (digitalRead(PIN_TOUCH_MISO)) {
            res |= (1U << i);
        }
        digitalWrite(PIN_TOUCH_CLK, LOW);
        delayMicroseconds(1);
    }
    // Drain last 3 trailing clocks
    for (int8_t i = 0; i < 3; i++) {
        digitalWrite(PIN_TOUCH_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(PIN_TOUCH_CLK, LOW);
        delayMicroseconds(1);
    }
    return res;
}

bool TouchInput::readRawPoint(uint16_t& rawX, uint16_t& rawY, uint16_t& rawZ) {
    // Check IRQ pin first (active LOW when touched) + verify pressure
    if (digitalRead(PIN_TOUCH_IRQ) == HIGH) {
        return false;
    }

    digitalWrite(PIN_TOUCH_CS, LOW);
    delayMicroseconds(2);

    uint16_t z1 = readXptChannel(0xB1);
    uint16_t z2 = readXptChannel(0xC1);
    int16_t z = (int16_t)z1 + 4095 - (int16_t)z2;
    if (z < 350) {
        readXptChannel(0x90); // power down IRQ re-enable
        digitalWrite(PIN_TOUCH_CS, HIGH);
        return false;
    }

    uint16_t xs[5], ys[5];
    for (int i = 0; i < 5; i++) {
        xs[i] = readXptChannel(0xD1);
        ys[i] = readXptChannel(0x91);
    }
    // Final dummy read with PD=00 to keep PENIRQ active
    readXptChannel(0x90);
    digitalWrite(PIN_TOUCH_CS, HIGH);

    // Median-3 (sort xs/ys and take middle 3 average)
    auto avgMid3 = [](uint16_t arr[5]) -> uint16_t {
        for (int i = 0; i < 4; i++) {
            for (int j = i + 1; j < 5; j++) {
                if (arr[j] < arr[i]) {
                    uint16_t t = arr[i];
                    arr[i] = arr[j];
                    arr[j] = t;
                }
            }
        }
        return (uint16_t)((arr[1] + arr[2] + arr[3]) / 3);
    };

    rawX = avgMid3(xs);
    rawY = avgMid3(ys);
    rawZ = (uint16_t)z;
    return true;
}

void TouchInput::mapRawToScreen(uint16_t rx, uint16_t ry, int16_t& sx, int16_t& sy) const {
    uint16_t ax = _cal.swapXY ? ry : rx;
    uint16_t ay = _cal.swapXY ? rx : ry;

    int32_t spanX = (int32_t)_cal.xMax - (int32_t)_cal.xMin;
    int32_t spanY = (int32_t)_cal.yMax - (int32_t)_cal.yMin;
    if (abs(spanX) < 100 || abs(spanY) < 100) {
        sx = CYD_SCREEN_W / 2;
        sy = CYD_SCREEN_H / 2;
        return;
    }

    int32_t mappedX = map((int32_t)ax, (int32_t)_cal.xMin, (int32_t)_cal.xMax, 0, CYD_SCREEN_W - 1);
    int32_t mappedY = map((int32_t)ay, (int32_t)_cal.yMin, (int32_t)_cal.yMax, 0, CYD_SCREEN_H - 1);

    mappedX = constrain(mappedX, 0, CYD_SCREEN_W - 1);
    mappedY = constrain(mappedY, 0, CYD_SCREEN_H - 1);

    if (_rotation == 3) {
        mappedX = (CYD_SCREEN_W - 1) - mappedX;
        mappedY = (CYD_SCREEN_H - 1) - mappedY;
    }

    sx = (int16_t)mappedX;
    sy = (int16_t)mappedY;
}

bool TouchInput::readScreenPoint(int16_t& sx, int16_t& sy) {
    uint16_t rx, ry, rz;
    if (!readRawPoint(rx, ry, rz)) return false;
    mapRawToScreen(rx, ry, sx, sy);
    return true;
}

TouchEvent TouchInput::poll() {
    TouchEvent ev;
    uint32_t now = millis();
    int16_t sx = 0, sy = 0;
    bool pressed = readScreenPoint(sx, sy);

    if (pressed) {
        ev.isDown = true;
        ev.x = sx;
        ev.y = sy;
        if (!_down) {
            if (now - _lastReleaseMs < 75) {
                return ev; // debounce contact bounce
            }
            _down = true;
            _longFired = false;
            _dragged = false;
            _startX = sx;
            _startY = sy;
            _lastX = sx;
            _lastY = sy;
            _downMs = now;
            _lastHoldEmitMs = now;
            ev.type = TouchEventType::PRESS;
            ev.startX = _startX;
            ev.startY = _startY;
            ev.holdMs = 0;
        } else {
            int16_t stepDx = abs(sx - _lastX);
            int16_t stepDy = abs(sy - _lastY);
            _lastX = sx;
            _lastY = sy;
            if (abs(sx - _startX) > 18 || abs(sy - _startY) > 18) {
                _dragged = true;
            }
            ev.startX = _startX;
            ev.startY = _startY;
            ev.holdMs = (uint16_t)min<uint32_t>(now - _downMs, 65000UL);
            if (!_dragged && !_longFired && (now - _downMs >= 560)) {
                _longFired = true;
                _lastHoldEmitMs = now;
                ev.type = TouchEventType::LONG_PRESS;
            } else if (stepDx >= 2 || stepDy >= 2 ||
                       (!_dragged && !_longFired && ev.holdMs >= 80 && (now - _lastHoldEmitMs >= 35)) ||
                       (!_dragged && _longFired && (now - _lastHoldEmitMs >= 145))) {
                _lastHoldEmitMs = now;
                ev.type = TouchEventType::DRAG_MOVE;
            }
        }
    } else if (_down) {
        _down = false;
        _lastReleaseMs = now;
        ev.isDown = false;
        ev.x = _lastX;
        ev.y = _lastY;
        ev.startX = _startX;
        ev.startY = _startY;
        ev.holdMs = (uint16_t)min<uint32_t>(now - _downMs, 65000UL);

        if (_longFired) {
            ev.type = TouchEventType::RELEASE;
        } else {
            int16_t dx = _lastX - _startX;
            int16_t dy = _lastY - _startY;
            if (abs(dx) >= 44 && abs(dx) > abs(dy) * 1.4f) {
                ev.type = (dx < 0) ? TouchEventType::SWIPE_LEFT : TouchEventType::SWIPE_RIGHT;
            } else if (abs(dy) >= 40 && abs(dy) > abs(dx) * 1.4f) {
                ev.type = (dy < 0) ? TouchEventType::SWIPE_UP : TouchEventType::SWIPE_DOWN;
            } else if (!_dragged && (now - _downMs) >= 18) {
                ev.type = TouchEventType::TAP;
                ev.x = (_startX + _lastX) / 2;
                ev.y = (_startY + _lastY) / 2;
            } else {
                ev.type = TouchEventType::RELEASE;
            }
        }
    }
    return ev;
}

void TouchInput::drawCalibrationTarget(TFT_eSPI& tft, uint8_t idx) {
    struct Target { int16_t x; int16_t y; const char* name; };
    static const Target pts[4] = {
        {24, 24, "TOP-LEFT"},
        {CYD_SCREEN_W - 25, 24, "TOP-RIGHT"},
        {24, CYD_SCREEN_H - 25, "BOT-LEFT"},
        {CYD_SCREEN_W - 25, CYD_SCREEN_H - 25, "BOT-RIGHT"}
    };
    const Target& p = pts[idx % 4];
    char stepBuf[32];
    snprintf(stepBuf, sizeof(stepBuf), "TARGET %d / 4: %s", (int)idx + 1, p.name);

    tft.fillScreen(Theme::BG_VOID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Theme::CYAN, Theme::BG_VOID);
    tft.drawString("TOUCH CALIBRATION", CYD_SCREEN_W / 2, 74, 2);
    tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_VOID);
    tft.drawString(stepBuf, CYD_SCREEN_W / 2, 106, 2);
    tft.setTextColor(Theme::TEXT_MUTED, Theme::BG_VOID);
    tft.drawString("Tap center crosshair / Press BOOT to cancel", CYD_SCREEN_W / 2, 134, 1);

    tft.drawCircle(p.x, p.y, 13, Theme::CYAN);
    tft.drawFastHLine(p.x - 18, p.y, 37, Theme::TEXT_PRIMARY);
    tft.drawFastVLine(p.x, p.y - 18, 37, Theme::TEXT_PRIMARY);
    tft.fillCircle(p.x, p.y, 3, Theme::AMBER);
}

void TouchInput::startCalibration(TFT_eSPI& tft) {
    _calActive = true;
    _calIdx = 0;
    _calSamples = 0;
    _calSumX = 0;
    _calSumY = 0;
    _calWaitRelease = (digitalRead(PIN_TOUCH_IRQ) == LOW);
    _calStageMs = millis();
    memset(_calRxs, 0, sizeof(_calRxs));
    memset(_calRys, 0, sizeof(_calRys));
    drawCalibrationTarget(tft, 0);
}

bool TouchInput::stepCalibration(TFT_eSPI& tft, TouchCalData& outCal, bool& finished, bool& ok) {
    finished = false;
    ok = false;
    if (!_calActive) return false;

    uint32_t now = millis();
    if (digitalRead(PIN_BOOT_BTN) == LOW) {
        cancelCalibration();
        finished = true;
        ok = false;
        return true;
    }

    if (now - _calStageMs > 20000) {
        cancelCalibration();
        finished = true;
        ok = false;
        return true;
    }

    static const int16_t ptsX[4] = { 24, CYD_SCREEN_W - 25, 24, CYD_SCREEN_W - 25 };
    static const int16_t ptsY[4] = { 24, 24, CYD_SCREEN_H - 25, CYD_SCREEN_H - 25 };

    if (_calWaitRelease) {
        if (digitalRead(PIN_TOUCH_IRQ) != LOW && (now - _calStageMs) >= 140) {
            _calWaitRelease = false;
            if (_calSamples == 0) {
                // Released initial pre-existing touch before point 0 capture
                _calStageMs = now;
                return false;
            }
            _calSamples = 0;
            if (_calIdx < 3) {
                _calIdx++;
                _calStageMs = now;
                drawCalibrationTarget(tft, _calIdx);
                return false;
            }

            // Compute calibrated bounds across all 4 corners
            int dxSpan = abs((int)_calRxs[1] - (int)_calRxs[0]);
            int dySpan = abs((int)_calRys[1] - (int)_calRys[0]);
            bool swap = (dySpan > dxSpan);

            auto pickX = [&](int i) -> uint16_t { return swap ? _calRys[i] : _calRxs[i]; };
            auto pickY = [&](int i) -> uint16_t { return swap ? _calRxs[i] : _calRys[i]; };

            int32_t leftRaw  = (pickX(0) + pickX(2)) / 2;
            int32_t rightRaw = (pickX(1) + pickX(3)) / 2;
            int32_t topRaw   = (pickY(0) + pickY(1)) / 2;
            int32_t botRaw   = (pickY(2) + pickY(3)) / 2;

            _calActive = false;
            finished = true;
            if (abs(rightRaw - leftRaw) < 400 || abs(botRaw - topRaw) < 400) {
                ok = false;
                return true;
            }

            float xScale = (float)(rightRaw - leftRaw) / (float)(CYD_SCREEN_W - 49);
            float yScale = (float)(botRaw - topRaw) / (float)(CYD_SCREEN_H - 49);

            int32_t xMin = (int32_t)(leftRaw - 24.0f * xScale);
            int32_t xMax = (int32_t)(rightRaw + 24.0f * xScale);
            int32_t yMin = (int32_t)(topRaw - 24.0f * yScale);
            int32_t yMax = (int32_t)(botRaw + 24.0f * yScale);

            if (_rotation == 3) {
                int32_t t = xMin; xMin = xMax; xMax = t;
                t = yMin; yMin = yMax; yMax = t;
            }

            outCal.swapXY = swap;
            outCal.xMin = (uint16_t)constrain(xMin, 0, 4095);
            outCal.xMax = (uint16_t)constrain(xMax, 0, 4095);
            outCal.yMin = (uint16_t)constrain(yMin, 0, 4095);
            outCal.yMax = (uint16_t)constrain(yMax, 0, 4095);
            outCal.calibrated = true;
            _cal = outCal;
            ok = true;
            return true;
        }
        return false;
    }

    uint16_t rx, ry, rz;
    if (readRawPoint(rx, ry, rz)) {
        _calSumX += rx;
        _calSumY += ry;
        _calSamples++;
        if (_calSamples >= 3) {
            _calRxs[_calIdx] = (uint16_t)(_calSumX / _calSamples);
            _calRys[_calIdx] = (uint16_t)(_calSumY / _calSamples);
            _calSumX = 0;
            _calSumY = 0;
            _calWaitRelease = true;
            _calStageMs = now;
            tft.drawCircle(ptsX[_calIdx], ptsY[_calIdx], 15, Theme::EMERALD);
            tft.fillCircle(ptsX[_calIdx], ptsY[_calIdx], 5, Theme::EMERALD);
        }
    } else if (_calSamples > 0) {
        // Premature lift before 3-sample median lock — discard partial accumulator cleanly
        _calSamples = 0;
        _calSumX = 0;
        _calSumY = 0;
    }
    return false;
}

bool TouchInput::runCalibrationWizard(TFT_eSPI& tft, TouchCalData& outCal, void (*idleCb)()) {
    startCalibration(tft);
    bool finished = false;
    bool ok = false;
    while (!finished) {
        if (idleCb) idleCb();
        stepCalibration(tft, outCal, finished, ok);
        delay(16);
    }
    return ok;
}
