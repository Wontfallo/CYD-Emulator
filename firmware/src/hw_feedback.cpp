#include "hw_feedback.h"
#include "board_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>

static QueueHandle_t s_sfxQueue = nullptr;
static volatile bool s_sfxEnabled = true;
static volatile bool s_audioTaskActive = false;

namespace {
inline void playToneBlocking(uint32_t freqHz, uint32_t ms) {
    if (freqHz == 0) {
        ledcWrite(PWM_CH_AUDIO, 0);
        delay(ms);
        return;
    }
    // Keep PWM resolution explicit at 8-bit so duty=110 maps consistently (~43% duty)
    ledcChangeFrequency(PWM_CH_AUDIO, (double)freqHz, PWM_RES_8BIT);
    ledcWrite(PWM_CH_AUDIO, 110);
    delay(ms);
    ledcWrite(PWM_CH_AUDIO, 0);
}
} // namespace

void HwFeedback::audioTask(void* param) {
    (void)param;
    SfxType sfx;
    for (;;) {
        if (xQueueReceive(s_sfxQueue, &sfx, portMAX_DELAY) == pdTRUE) {
            if (!s_sfxEnabled) continue;
            digitalWrite(PIN_AUDIO_EN, LOW); // Enable SC8002B / FM8002E amp
            delay(6); // Let bias settle cleanly to minimize turn-on click
            switch (sfx) {
                case SfxType::SFX_TAP:
                    playToneBlocking(1350, 9);
                    break;
                case SfxType::SFX_TOGGLE_ON:
                    playToneBlocking(920, 14);
                    playToneBlocking(1480, 18);
                    break;
                case SfxType::SFX_TOGGLE_OFF:
                    playToneBlocking(1180, 14);
                    playToneBlocking(740, 18);
                    break;
                case SfxType::SFX_PAGE:
                    playToneBlocking(820, 10);
                    playToneBlocking(1060, 10);
                    break;
                case SfxType::SFX_ERROR:
                    playToneBlocking(320, 45);
                    delay(12);
                    playToneBlocking(240, 65);
                    break;
                case SfxType::SFX_BOOT:
                    playToneBlocking(659, 30);
                    playToneBlocking(880, 35);
                    playToneBlocking(1318, 55);
                    break;
                case SfxType::SFX_ALARM:
                    for (int i = 0; i < 3; i++) {
                        playToneBlocking(1200, 60);
                        delay(45);
                    }
                    break;
            }
            delay(6);
            digitalWrite(PIN_AUDIO_EN, HIGH); // Mute amp to avoid idle hiss
        } else {
#if defined(HYDRO_VIRTUAL_CYD)
            break;
#endif
        }
    }
    s_audioTaskActive = false;
}

void HwFeedback::begin(const DeviceSettings& cfg) {
    _cfg = cfg;
    s_sfxEnabled = cfg.sfxEnabled;

    // Backlight PWM (Arduino-ESP32 2.x LEDC)
    ledcSetup(PWM_CH_BL, PWM_FREQ_BL, PWM_RES_8BIT);
    ledcAttachPin(PIN_TFT_BL, PWM_CH_BL);
    setBrightnessImmediate(cfg.brightness);

    // RGB common anode LED (active LOW)
    ledcSetup(PWM_CH_LEDR, PWM_FREQ_LED, PWM_RES_8BIT);
    ledcSetup(PWM_CH_LEDG, PWM_FREQ_LED, PWM_RES_8BIT);
    ledcSetup(PWM_CH_LEDB, PWM_FREQ_LED, PWM_RES_8BIT);
    ledcAttachPin(PIN_LED_R, PWM_CH_LEDR);
    ledcAttachPin(PIN_LED_G, PWM_CH_LEDG);
    ledcAttachPin(PIN_LED_B, PWM_CH_LEDB);
    writeRgb(0, 180, 220);

    // Audio amp + DAC PWM
    pinMode(PIN_AUDIO_EN, OUTPUT);
    digitalWrite(PIN_AUDIO_EN, HIGH);
    ledcSetup(PWM_CH_AUDIO, 2000, PWM_RES_8BIT);
    ledcAttachPin(PIN_AUDIO_DAC, PWM_CH_AUDIO);
    ledcWrite(PWM_CH_AUDIO, 0);

    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    pinMode(PIN_BAT_ADC, INPUT);

    if (!s_sfxQueue) {
        s_sfxQueue = xQueueCreate(8, sizeof(SfxType));
    }
#if defined(HYDRO_VIRTUAL_CYD)
    xQueueReset(s_sfxQueue);
#endif
    if (!s_audioTaskActive) {
        s_audioTaskActive = true;
        xTaskCreatePinnedToCore(audioTask, "hydro_sfx", 2048, nullptr, 1, nullptr, 0);
    }

    _lastActivityMs = millis();
}

void HwFeedback::applySettings(const DeviceSettings& cfg) {
    _cfg = cfg;
    s_sfxEnabled = cfg.sfxEnabled;
    if (!_sleeping) {
        _targetBl = constrain(cfg.brightness, 25, 255);
    }
}

void HwFeedback::setBrightnessImmediate(uint8_t val) {
    val = constrain(val, 0, 255);
    _curBl = val;
    _targetBl = val;
    ledcWrite(PWM_CH_BL, val);
}

void HwFeedback::notifyActivity() {
    _lastActivityMs = millis();
    if (_sleeping) {
        wake();
    }
}

void HwFeedback::wake() {
    _sleeping = false;
    _targetBl = constrain(_cfg.brightness, 25, 255);
    _lastActivityMs = millis();
}

void HwFeedback::sleep() {
    _sleeping = true;
    _targetBl = 0;
}

void HwFeedback::writeRgb(uint8_t r, uint8_t g, uint8_t b) {
    // Active LOW RGB LED (0 = bright, 255 = off)
    ledcWrite(PWM_CH_LEDR, 255 - r);
    ledcWrite(PWM_CH_LEDG, 255 - g);
    ledcWrite(PWM_CH_LEDB, 255 - b);
}

void HwFeedback::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs) {
    _pulseR = r;
    _pulseG = g;
    _pulseB = b;
    _pulseUntilMs = millis() + durationMs;
}

void HwFeedback::playSfx(SfxType sfx) {
    if (!_cfg.sfxEnabled || !s_sfxQueue) return;
    xQueueSend(s_sfxQueue, &sfx, 0);
}

void HwFeedback::loop(float maxLoadPct, bool isHot, bool sleeping) {
    uint32_t now = millis();

    // Auto-sleep check
    if (!_sleeping && _cfg.sleepSec > 0) {
        if ((now - _lastActivityMs) > (uint32_t)_cfg.sleepSec * 1000UL) {
            sleep();
        }
    }

    // Smooth backlight ramp
    if (now - _lastBlTickMs >= 16) {
        _lastBlTickMs = now;
        if (_curBl != _targetBl) {
            int diff = (int)_targetBl - (int)_curBl;
            int step = abs(diff) > 12 ? diff / 3 : (diff > 0 ? 2 : -2);
            if (abs(diff) <= 2) step = diff;
            _curBl = (uint8_t)constrain((int)_curBl + step, 0, 255);
            ledcWrite(PWM_CH_BL, _curBl);
        }
    }

    // RGB LED status
    if (sleeping || _sleeping) {
        // Ultra subtle standby breathe
        float ph = (sinf(now * 0.0018f) + 1.0f) * 0.5f;
        writeRgb(0, (uint8_t)(4 + ph * 12), (uint8_t)(8 + ph * 18));
        return;
    }

    if ((int32_t)(_pulseUntilMs - now) > 0) {
        writeRgb(_pulseR, _pulseG, _pulseB);
        return;
    }

    switch (_cfg.ledMode) {
        case LED_VITALS: {
            float load = constrain(maxLoadPct / 100.0f, 0.0f, 1.0f);
            float breath = 0.45f + 0.55f * ((sinf(now * 0.003f) + 1.0f) * 0.5f);
            uint8_t r = 0, g = 0, b = 0;
            if (isHot || load > 0.80f) {
                r = (uint8_t)(210 * breath);
                g = (uint8_t)(28 * breath);
                b = (uint8_t)(42 * breath);
            } else if (load > 0.45f) {
                float t = (load - 0.45f) / 0.35f;
                r = (uint8_t)((30 + t * 210) * breath);
                g = (uint8_t)((210 - t * 90) * breath);
                b = (uint8_t)((230 - t * 200) * breath);
            } else {
                float t = load / 0.45f;
                r = (uint8_t)(t * 30 * breath);
                g = (uint8_t)((160 + t * 65) * breath);
                b = (uint8_t)((230 - t * 60) * breath);
            }
            writeRgb(r, g, b);
            break;
        }
        case LED_CYCLE: {
            float h = fmodf(now * 0.0005f, 1.0f) * 6.0f;
            int i = (int)h;
            float f = h - i;
            float q = 1.0f - f;
            float r = 0, g = 0, b = 0;
            switch (i % 6) {
                case 0: r = 1; g = f; b = 0; break;
                case 1: r = q; g = 1; b = 0; break;
                case 2: r = 0; g = 1; b = f; break;
                case 3: r = 0; g = q; b = 1; break;
                case 4: r = f; g = 0; b = 1; break;
                case 5: r = 1; g = 0; b = q; break;
            }
            writeRgb((uint8_t)(r * 140), (uint8_t)(g * 140), (uint8_t)(b * 140));
            break;
        }
        case LED_CYAN: {
            float breath = 0.55f + 0.45f * ((sinf(now * 0.0025f) + 1.0f) * 0.5f);
            writeRgb(0, (uint8_t)(160 * breath), (uint8_t)(210 * breath));
            break;
        }
        case LED_OFF:
        default:
            writeRgb(0, 0, 0);
            break;
    }
}

BootBtnAction HwFeedback::pollBootButton() {
    bool cur = digitalRead(PIN_BOOT_BTN);
    uint32_t now = millis();
    BootBtnAction act = BootBtnAction::NONE;

    if (!cur && _bootLast) {
        _bootDownMs = now;
        _bootLongFired = false;
    } else if (!cur && !_bootLast) {
        if (!_bootLongFired && (now - _bootDownMs >= 1100)) {
            _bootLongFired = true;
            act = BootBtnAction::LONG_PRESS;
        }
    } else if (cur && !_bootLast) {
        if (!_bootLongFired && (now - _bootDownMs >= 45)) {
            act = BootBtnAction::SHORT_PRESS;
        }
    }

    _bootLast = cur;
    return act;
}

void HwFeedback::clearBootButton() {
    _bootLast = digitalRead(PIN_BOOT_BTN);
    _bootDownMs = millis();
    _bootLongFired = false;
}

float HwFeedback::readBatteryVolts() {
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += analogReadMilliVolts(PIN_BAT_ADC);
    }
    return (sum / 4.0f) * 2.0f / 1000.0f;
}
