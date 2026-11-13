#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace vcyd {

struct AudioToneEvent {
    uint32_t ts_ms;
    uint16_t freq_hz;
    uint16_t duration_ms;
    bool amp_enabled;
};

struct HwStateSnapshot {
    uint32_t uptime_ms = 0;
    uint8_t panel_type = 0;      // 0 = ILI9341, 1 = ST7789
    uint8_t backlight_pwm = 0;   // 0..255 (PIN_TFT_BL PWM)
    uint8_t led_r = 0;           // decoded logical intensity 0..255 (common anode inverted)
    uint8_t led_g = 0;
    uint8_t led_b = 0;
    uint8_t raw_pwm_r = 255;
    uint8_t raw_pwm_g = 255;
    uint8_t raw_pwm_b = 255;
    bool amp_enabled = false;
    bool boot_pressed = false;
    bool touch_irq_active = false;
    uint16_t touch_raw_x = 0;
    uint16_t touch_raw_y = 0;
    uint16_t bat_mv = 4120;
    uint8_t madctl = 0x20;
    bool inverted = false;
    uint8_t rotation = 1;
    std::string screen_name = "DECK";
    int page_idx = 0;
    bool sleeping = false;
    bool calibrating = false;
    std::string wifi_ssid = "HYDRO-STUDIO-5G";
    std::string wifi_ip = "192.168.1.48";
    bool wifi_connected = false;
};

class VirtualHw {
public:
    static VirtualHw& instance();

    void reset(const std::string& storage_dir, uint8_t panel_type);
    void requestShutdown();
    bool shutdownRequested() const;

    // Clocks (host real-time + fast sleep helper)
    uint32_t millis();
    uint32_t micros();
    void delayMs(uint32_t ms);
    void delayUs(uint32_t us);

    // GPIO & XPT2046 Resistive Touch IC simulation
    void pinMode(uint8_t pin, uint8_t mode);
    void digitalWrite(uint8_t pin, uint8_t val);
    int digitalRead(uint8_t pin);
    uint16_t analogReadMilliVolts(uint8_t pin);

    // Host inputs into virtual hardware
    void setTouchRaw(bool down, uint16_t raw_x, uint16_t raw_y);
    void setBootPressed(bool down);
    void setBatteryMv(uint16_t mv);
    void setPanelType(uint8_t panel_type);
    uint8_t panelType() const;

    // LEDC PWM (Arduino ESP32 core 2.x compatible)
    double ledcSetup(uint8_t ch, double freq, uint8_t resolution_bits);
    void ledcAttachPin(uint8_t pin, uint8_t ch);
    void ledcWrite(uint8_t ch, uint32_t duty);
    double ledcWriteTone(uint8_t ch, double freq);
    double ledcChangeFrequency(uint8_t ch, double freq, uint8_t resolution_bits);

    // Serial RX/TX streams (Host <-> ESP32 USB-UART)
    void hostPushRx(const std::string& data);
    int serialAvailable();
    int serialRead();
    int serialPeek();
    void serialTxWrite(const uint8_t* data, size_t len);
    std::string hostPopTxAll();

    // Persistent storage directories
    std::string littlefsRoot() const;
    std::string prefsDir() const;

    // Audio events
    std::vector<AudioToneEvent> popAudioEvents();

    // Display telemetry metadata updated by TFT_eSPI_emu & firmware bridge
    void setDisplayMeta(uint8_t rotation, uint8_t madctl, bool inverted);
    void setUiMeta(const std::string& screen_name, int page_idx, bool sleeping, bool calibrating);
    void setWifiMeta(bool connected, const std::string& ssid, const std::string& ip);
    HwStateSnapshot snapshot();

    // Thread lifecycle registry (stops detached audio task cleanly on shutdown/reset)
    void registerWorker(std::function<void()> join_fn);
    void joinAllWorkers();

private:
    VirtualHw() = default;
    void onXptSpiEdge(bool cs_low, bool clk_high, bool mosi_high);
    void flushToneSegment();

    std::atomic<bool> _shutdown{false};
    std::atomic<bool> _touchDown{false};
    std::atomic<uint16_t> _touchRawX{2048};
    std::atomic<uint16_t> _touchRawY{2048};
    std::atomic<bool> _bootDown{false};
    std::atomic<uint16_t> _batMv{4120};
    std::atomic<uint8_t> _panelType{0};

    int64_t _t0_us = 0;

    mutable std::mutex _gpioMtx;
    uint8_t _pinVal[48]{};
    uint8_t _pinMode[48]{};
    int8_t _pinToCh[48]{};

    struct LedcCh {
        double freq = 5000.0;
        uint8_t res_bits = 8;
        uint32_t duty = 0;
        int8_t pin = -1;
    } _ledc[16];

    // XPT2046 SPI state machine
    bool _prevCsLow = false;
    bool _prevClkHigh = false;
    uint8_t _xptCmdBits = 0;
    uint8_t _xptCmd = 0;
    bool _xptInDataPhase = false;
    int _xptDataClockIdx = 0; // clock 0 = BUSY low, clocks 1..12 = DB11..DB0, clocks 13..15 = 0
    uint16_t _xptShiftOut = 0;
    bool _xptMisoLevel = false;

    // Active audio tone tracker
    uint32_t _toneStartMs = 0;
    uint16_t _toneFreq = 0;
    bool _toneActive = false;
    std::deque<AudioToneEvent> _audioEvents;

    // Serial rings
    mutable std::mutex _serialMtx;
    std::deque<uint8_t> _rxBuf;
    std::string _txBuf;

    // Paths & display meta
    mutable std::mutex _metaMtx;
    std::string _storageDir = "companion/data/virtual_cyd";
    uint8_t _rotation = 1;
    uint8_t _madctl = 0x20;
    bool _inverted = false;
    std::string _screenName = "DECK";
    int _pageIdx = 0;
    bool _sleeping = false;
    bool _calibrating = false;
    bool _wifiConnected = false;
    std::string _wifiSsid = "HYDRO-STUDIO-5G";
    std::string _wifiIp = "192.168.1.48";

    std::mutex _workerMtx;
    std::vector<std::function<void()>> _workers;
};

} // namespace vcyd

