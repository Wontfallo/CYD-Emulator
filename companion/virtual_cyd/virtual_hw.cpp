#include "virtual_hw.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace vcyd {

namespace {
int64_t steadyNowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

VirtualHw& VirtualHw::instance() {
    static VirtualHw inst;
    return inst;
}

void VirtualHw::reset(const std::string& storage_dir, uint8_t panel_type) {
    requestShutdown();
    joinAllWorkers();

    _shutdown.store(false);
    _panelType.store(panel_type);
    _t0_us = steadyNowUs();

    {
        std::lock_guard<std::mutex> lk(_gpioMtx);
        std::memset(_pinVal, 0, sizeof(_pinVal));
        std::memset(_pinMode, 0, sizeof(_pinMode));
        for (int i = 0; i < 48; ++i) _pinToCh[i] = -1;
        for (int i = 0; i < 16; ++i) {
            _ledc[i] = {};
        }
        _pinVal[36] = 1; // PENIRQ idle HIGH
        _pinVal[0] = 1;  // BOOT pull-up idle HIGH
        _pinVal[4] = 1;  // AMP disabled idle HIGH
        _prevCsLow = false;
        _prevClkHigh = false;
        _xptCmdBits = 0;
        _xptCmd = 0;
        _xptInDataPhase = false;
        _xptDataClockIdx = 0;
        _xptShiftOut = 0;
        _xptMisoLevel = false;
        _toneActive = false;
        _toneFreq = 0;
        _toneStartMs = 0;
        _audioEvents.clear();
    }

    {
        std::lock_guard<std::mutex> lk(_serialMtx);
        _rxBuf.clear();
        _txBuf.clear();
    }

    {
        std::lock_guard<std::mutex> lk(_metaMtx);
        _storageDir = storage_dir.empty() ? "companion/data/virtual_cyd" : storage_dir;
        _rotation = 1;
        _madctl = (panel_type == 1) ? 0x60 : 0x28;
        _inverted = false;
        _screenName = "DECK";
        _pageIdx = 0;
        _sleeping = false;
        _calibrating = false;
    }

    _touchDown.store(false);
    _bootDown.store(false);
}

void VirtualHw::requestShutdown() {
    _shutdown.store(true);
}

bool VirtualHw::shutdownRequested() const {
    return _shutdown.load();
}

uint32_t VirtualHw::millis() {
    if (_t0_us == 0) _t0_us = steadyNowUs();
    int64_t d = (steadyNowUs() - _t0_us) / 1000;
    return static_cast<uint32_t>(d > 0 ? d : 0);
}

uint32_t VirtualHw::micros() {
    if (_t0_us == 0) _t0_us = steadyNowUs();
    int64_t d = steadyNowUs() - _t0_us;
    return static_cast<uint32_t>(d > 0 ? d : 0);
}

void VirtualHw::delayMs(uint32_t ms) {
    if (ms == 0 || _shutdown.load()) return;
    // Sleep in small slices so shutdown or state transitions stay responsive
    uint32_t rem = ms;
    while (rem > 0 && !_shutdown.load()) {
        uint32_t step = rem > 10 ? 10 : rem;
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        rem -= step;
    }
}

void VirtualHw::delayUs(uint32_t us) {
    // Bit-bang SPI timing (1-2us) runs instantaneously in emulator without starving host CPU
    if (us > 1000 && !_shutdown.load()) {
        delayMs(us / 1000);
    }
}

void VirtualHw::pinMode(uint8_t pin, uint8_t mode) {
    if (pin >= 48) return;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    _pinMode[pin] = mode;
    if (mode == 2) { // INPUT_PULLUP
        _pinVal[pin] = 1;
    }
}

void VirtualHw::digitalWrite(uint8_t pin, uint8_t val) {
    if (pin >= 48) return;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    uint8_t norm = val ? 1 : 0;
    _pinVal[pin] = norm;

    // PIN_AUDIO_EN = 4 (LOW = active, HIGH = shutdown)
    if (pin == 4 && norm == 1) {
        flushToneSegment();
    }

    // Track XPT2046 bit-banged SPI signals: CS=33, CLK=25, MOSI=32
    if (pin == 33 || pin == 25 || pin == 32) {
        onXptSpiEdge(_pinVal[33] == 0, _pinVal[25] != 0, _pinVal[32] != 0);
    }
}

int VirtualHw::digitalRead(uint8_t pin) {
    if (pin == 0) { // PIN_BOOT_BTN active LOW
        return _bootDown.load() ? 0 : 1;
    }
    if (pin == 36) { // PIN_TOUCH_IRQ active LOW
        return _touchDown.load() ? 0 : 1;
    }
    if (pin == 39) { // PIN_TOUCH_MISO
        std::lock_guard<std::mutex> lk(_gpioMtx);
        return _xptMisoLevel ? 1 : 0;
    }
    if (pin >= 48) return 0;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    return _pinVal[pin] ? 1 : 0;
}

uint16_t VirtualHw::analogReadMilliVolts(uint8_t pin) {
    if (pin == 34) {
        // PIN_BAT_ADC measures half of battery via 100k/100k divider
        return static_cast<uint16_t>(_batMv.load() / 2);
    }
    return 0;
}

void VirtualHw::onXptSpiEdge(bool cs_low, bool clk_high, bool mosi_high) {
    if (!cs_low) {
        _prevCsLow = false;
        _prevClkHigh = clk_high;
        _xptCmdBits = 0;
        _xptInDataPhase = false;
        _xptDataClockIdx = 0;
        _xptMisoLevel = false;
        return;
    }

    if (!_prevCsLow) {
        // CS falling edge resets transaction
        _prevCsLow = true;
        _prevClkHigh = clk_high;
        _xptCmdBits = 0;
        _xptCmd = 0;
        _xptInDataPhase = false;
        _xptDataClockIdx = 0;
        _xptMisoLevel = false;
        return;
    }

    bool rising = (!_prevClkHigh && clk_high);
    bool falling = (_prevClkHigh && !clk_high);
    _prevClkHigh = clk_high;

    if (rising) {
        if (!_xptInDataPhase) {
            _xptCmd = static_cast<uint8_t>((_xptCmd << 1) | (mosi_high ? 1 : 0));
            _xptCmdBits++;
        }
    } else if (falling) {
        if (!_xptInDataPhase) {
            if (_xptCmdBits >= 8) {
                // Finished 8-bit XPT2046 control byte.
                // TI XPT2046 timing: Clock 9 rising edge outputs BUSY=0 on DOUT,
                // then falling edge 9 places DB11 onto DOUT for clocks 10..21!
                _xptInDataPhase = true;
                _xptDataClockIdx = 0;
                uint16_t sample = 0;
                uint8_t ctrl = static_cast<uint8_t>(_xptCmd & 0xF0);
                if (ctrl == 0xD0) {
                    sample = _touchDown.load() ? _touchRawX.load() : 0;
                } else if (ctrl == 0x90) {
                    if ((_xptCmd & 0x03) == 0x00) {
                        sample = 0; // power-down dummy read
                    } else {
                        sample = _touchDown.load() ? _touchRawY.load() : 0;
                    }
                } else if (ctrl == 0xB0) {
                    sample = _touchDown.load() ? 1850 : 0; // Z1 touched pressure
                } else if (ctrl == 0xC0) {
                    sample = _touchDown.load() ? 2250 : 4095; // Z2 touched pressure => z = 1850+4095-2250 = 3695
                }
                _xptShiftOut = static_cast<uint16_t>(sample & 0x0FFF);
                _xptMisoLevel = false; // BUSY bit = 0 for the next clock
            }
        } else {
            _xptDataClockIdx++;
            if (_xptDataClockIdx >= 1 && _xptDataClockIdx <= 12) {
                int bitPos = 12 - _xptDataClockIdx; // 11 down to 0
                _xptMisoLevel = ((_xptShiftOut >> bitPos) & 0x1) != 0;
            } else {
                _xptMisoLevel = false;
                if (_xptDataClockIdx >= 16) {
                    // Ready for next command byte in continuous transaction
                    _xptInDataPhase = false;
                    _xptCmdBits = 0;
                    _xptCmd = 0;
                }
            }
        }
    }
}

void VirtualHw::setTouchRaw(bool down, uint16_t raw_x, uint16_t raw_y) {
    _touchRawX.store(raw_x > 4095 ? 4095 : raw_x);
    _touchRawY.store(raw_y > 4095 ? 4095 : raw_y);
    _touchDown.store(down);
}

void VirtualHw::setBootPressed(bool down) {
    _bootDown.store(down);
}

void VirtualHw::setBatteryMv(uint16_t mv) {
    _batMv.store(mv);
}

void VirtualHw::setPanelType(uint8_t panel_type) {
    _panelType.store(panel_type);
    std::lock_guard<std::mutex> lk(_metaMtx);
    _madctl = (panel_type == 1) ? 0x60 : 0x28;
}

uint8_t VirtualHw::panelType() const {
    return _panelType.load();
}

double VirtualHw::ledcSetup(uint8_t ch, double freq, uint8_t resolution_bits) {
    if (ch >= 16) return 0.0;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    _ledc[ch].freq = freq;
    _ledc[ch].res_bits = resolution_bits ? resolution_bits : 8;
    return freq;
}

void VirtualHw::ledcAttachPin(uint8_t pin, uint8_t ch) {
    if (ch >= 16 || pin >= 48) return;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    _ledc[ch].pin = static_cast<int8_t>(pin);
    _pinToCh[pin] = static_cast<int8_t>(ch);
}

void VirtualHw::flushToneSegment() {
    // caller holds _gpioMtx
    if (_toneActive) {
        uint32_t now = millis();
        uint32_t dur = (now >= _toneStartMs) ? (now - _toneStartMs) : 10;
        if (dur == 0) dur = 10;
        if (dur > 2000) dur = 2000;
        AudioToneEvent ev{
            _toneStartMs,
            _toneFreq,
            static_cast<uint16_t>(dur),
            _pinVal[4] == 0
        };
        if (_audioEvents.size() < 64) {
            _audioEvents.push_back(ev);
        }
        _toneActive = false;
    }
}

void VirtualHw::ledcWrite(uint8_t ch, uint32_t duty) {
    if (ch >= 16) return;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    _ledc[ch].duty = duty;
    if (ch == 4) { // PWM_CH_AUDIO
        if (duty > 0 && _ledc[ch].freq > 20.0) {
            if (!_toneActive) {
                _toneActive = true;
                _toneStartMs = millis();
                _toneFreq = static_cast<uint16_t>(_ledc[ch].freq);
            }
        } else {
            flushToneSegment();
        }
    }
}

double VirtualHw::ledcWriteTone(uint8_t ch, double freq) {
    if (ch >= 16) return 0.0;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    flushToneSegment();
    _ledc[ch].freq = freq;
    // Matches real ESP32 Arduino core 2.x behavior: ledcWriteTone forces 10-bit resolution!
    _ledc[ch].res_bits = 10;
    _ledc[ch].duty = (freq > 0.0) ? 512 : 0;
    if (ch == 4 && freq > 20.0) {
        _toneActive = true;
        _toneStartMs = millis();
        _toneFreq = static_cast<uint16_t>(freq);
    }
    return freq;
}

double VirtualHw::ledcChangeFrequency(uint8_t ch, double freq, uint8_t resolution_bits) {
    if (ch >= 16) return 0.0;
    std::lock_guard<std::mutex> lk(_gpioMtx);
    _ledc[ch].freq = freq;
    _ledc[ch].res_bits = resolution_bits ? resolution_bits : 8;
    return freq;
}

void VirtualHw::hostPushRx(const std::string& data) {
    if (data.empty()) return;
    std::lock_guard<std::mutex> lk(_serialMtx);
    for (unsigned char c : data) {
        if (_rxBuf.size() < 32768) {
            _rxBuf.push_back(c);
        }
    }
}

int VirtualHw::serialAvailable() {
    std::lock_guard<std::mutex> lk(_serialMtx);
    return static_cast<int>(_rxBuf.size());
}

int VirtualHw::serialRead() {
    std::lock_guard<std::mutex> lk(_serialMtx);
    if (_rxBuf.empty()) return -1;
    uint8_t c = _rxBuf.front();
    _rxBuf.pop_front();
    return static_cast<int>(c);
}

int VirtualHw::serialPeek() {
    std::lock_guard<std::mutex> lk(_serialMtx);
    if (_rxBuf.empty()) return -1;
    return static_cast<int>(_rxBuf.front());
}

void VirtualHw::serialTxWrite(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    std::lock_guard<std::mutex> lk(_serialMtx);
    if (_txBuf.size() < 65536) {
        _txBuf.append(reinterpret_cast<const char*>(data), len);
    }
}

std::string VirtualHw::hostPopTxAll() {
    std::lock_guard<std::mutex> lk(_serialMtx);
    std::string out;
    out.swap(_txBuf);
    return out;
}

std::string VirtualHw::littlefsRoot() const {
    std::lock_guard<std::mutex> lk(_metaMtx);
    return _storageDir + "/littlefs";
}

std::string VirtualHw::prefsDir() const {
    std::lock_guard<std::mutex> lk(_metaMtx);
    return _storageDir + "/nvs";
}

std::vector<AudioToneEvent> VirtualHw::popAudioEvents() {
    std::lock_guard<std::mutex> lk(_gpioMtx);
    std::vector<AudioToneEvent> out(_audioEvents.begin(), _audioEvents.end());
    _audioEvents.clear();
    return out;
}

void VirtualHw::setDisplayMeta(uint8_t rotation, uint8_t madctl, bool inverted) {
    std::lock_guard<std::mutex> lk(_metaMtx);
    _rotation = rotation;
    _madctl = madctl;
    _inverted = inverted;
}

void VirtualHw::setUiMeta(const std::string& screen_name, int page_idx, bool sleeping, bool calibrating) {
    std::lock_guard<std::mutex> lk(_metaMtx);
    _screenName = screen_name;
    _pageIdx = page_idx;
    _sleeping = sleeping;
    _calibrating = calibrating;
}

void VirtualHw::setWifiMeta(bool connected, const std::string& ssid, const std::string& ip) {
    std::lock_guard<std::mutex> lk(_metaMtx);
    _wifiConnected = connected;
    _wifiSsid = ssid;
    _wifiIp = ip;
}

HwStateSnapshot VirtualHw::snapshot() {
    HwStateSnapshot s;
    s.uptime_ms = millis();
    s.panel_type = _panelType.load();
    s.boot_pressed = _bootDown.load();
    s.touch_irq_active = _touchDown.load();
    s.touch_raw_x = _touchRawX.load();
    s.touch_raw_y = _touchRawY.load();
    s.bat_mv = _batMv.load();

    {
        std::lock_guard<std::mutex> lk(_gpioMtx);
        auto dutyTo8 = [](const LedcCh& ch) -> uint8_t {
            uint32_t maxDuty = (ch.res_bits >= 1 && ch.res_bits <= 15) ? ((1u << ch.res_bits) - 1u) : 255u;
            uint32_t d = std::min(ch.duty, maxDuty);
            return static_cast<uint8_t>((d * 255u) / maxDuty);
        };
        s.backlight_pwm = dutyTo8(_ledc[0]); // PWM_CH_BL
        s.raw_pwm_r = dutyTo8(_ledc[1]);     // PWM_CH_LEDR (common anode: 255=off, 0=full)
        s.raw_pwm_g = dutyTo8(_ledc[2]);     // PWM_CH_LEDG
        s.raw_pwm_b = dutyTo8(_ledc[3]);     // PWM_CH_LEDB
        s.led_r = static_cast<uint8_t>(255 - s.raw_pwm_r);
        s.led_g = static_cast<uint8_t>(255 - s.raw_pwm_g);
        s.led_b = static_cast<uint8_t>(255 - s.raw_pwm_b);
        s.amp_enabled = (_pinVal[4] == 0);
    }

    {
        std::lock_guard<std::mutex> lk(_metaMtx);
        s.madctl = _madctl;
        s.inverted = _inverted;
        s.rotation = _rotation;
        s.screen_name = _screenName;
        s.page_idx = _pageIdx;
        s.sleeping = _sleeping;
        s.calibrating = _calibrating;
        s.wifi_connected = _wifiConnected;
        s.wifi_ssid = _wifiSsid;
        s.wifi_ip = _wifiIp;
    }
    return s;
}

void VirtualHw::registerWorker(std::function<void()> join_fn) {
    std::lock_guard<std::mutex> lk(_workerMtx);
    _workers.push_back(std::move(join_fn));
}

void VirtualHw::joinAllWorkers() {
    std::vector<std::function<void()>> list;
    {
        std::lock_guard<std::mutex> lk(_workerMtx);
        list.swap(_workers);
    }
    for (auto& fn : list) {
        if (fn) fn();
    }
}

} // namespace vcyd

