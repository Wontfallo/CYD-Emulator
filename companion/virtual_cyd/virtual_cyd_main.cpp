// Virtual CYD DLL Entrypoint: Compiles and runs ACTUAL CYD C++ firmware against our ESP32/TFT/XPT2046 HAL!

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <WiFi.h>

#include "board_config.h"
#include "deck_store.h"
#include "display_ui.h"
#include "hw_feedback.h"
#include "protocol.h"
#include "touch_input.h"
#include "ui_theme.h"
#include "virtual_hw.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#define VCYD_EXPORT extern "C" __declspec(dllexport)

// Compile actual firmware main.cpp directly so 100% of firmware serial protocol & event dispatch runs unmodified!
#include "../../firmware/src/main.cpp"

namespace {

std::mutex g_apiMtx;
bool g_initialized = false;
std::vector<uint8_t> g_frontRgba(320 * 240 * 4, 0);

const char* screenNameStr(AppScreen s) {
    switch (s) {
        case SCR_DECK: return "DECK";
        case SCR_VITALS: return "VITALS";
        case SCR_MEDIA: return "MEDIA";
        case SCR_TIMER: return "TIMER";
        case SCR_SYS: return "SYS";
        default: return "DECK";
    }
}

void refreshFrontFramebuffer() {
    auto snap = vcyd::VirtualHw::instance().snapshot();
    g_ui.tft().exportPanelRgba(g_frontRgba.data(), snap.backlight_pwm);
}

void syncUiMeta() {
    vcyd::VirtualHw::instance().setUiMeta(
        screenNameStr(g_ui.currentScreen()),
        static_cast<int>(g_ui.currentPage()),
        g_hw.isSleeping(),
        g_touch.isCalibrating()
    );
}

} // namespace

VCYD_EXPORT int vcyd_init(const char* storage_dir, int panel_type) {
    std::lock_guard<std::mutex> lk(g_apiMtx);
    vcyd::VirtualHw::instance().reset(storage_dir ? storage_dir : "companion/data/virtual_cyd",
                                      panel_type ? 1 : 0);
    g_rxLine = "";
    g_lastTelemDrawMs = 0;
    g_lastHeartbeatMs = 0;
    g_wifiActive = false;
    g_wifiConfigured = false;
    g_wifiRoutesBound = false;
    g_lastWifiAttemptMs = 0;
    g_pendingRemoteCal = false;
    g_pendingDisplayApply = false;
    g_bootTapCount = 0;
    g_bootLastTapMs = 0;

    ::setup();
    syncUiMeta();
    refreshFrontFramebuffer();
    g_initialized = true;
    return 1;
}

VCYD_EXPORT void vcyd_shutdown() {
    std::lock_guard<std::mutex> lk(g_apiMtx);
    vcyd::VirtualHw::instance().requestShutdown();
    vcyd::VirtualHw::instance().joinAllWorkers();
    g_initialized = false;
}

VCYD_EXPORT void vcyd_set_panel_type(int panel_type) {
    std::lock_guard<std::mutex> lk(g_apiMtx);
    vcyd::VirtualHw::instance().setPanelType(panel_type ? 1 : 0);
    if (g_initialized) {
        g_ui.applyDisplaySettings(g_store.settings);
        g_ui.markDirty();
        g_ui.render(g_store);
        refreshFrontFramebuffer();
    }
}

VCYD_EXPORT void vcyd_tick() {
    std::lock_guard<std::mutex> lk(g_apiMtx);
    if (!g_initialized) return;
    ::loop();
    syncUiMeta();
    refreshFrontFramebuffer();
}

VCYD_EXPORT void vcyd_feed_serial(const char* utf8_lines) {
    if (!utf8_lines) return;
    vcyd::VirtualHw::instance().hostPushRx(std::string(utf8_lines));
}

VCYD_EXPORT int vcyd_pop_serial(char* out_buf, int max_len) {
    if (!out_buf || max_len <= 0) return 0;
    std::string tx = vcyd::VirtualHw::instance().hostPopTxAll();
    int n = static_cast<int>(min<size_t>(tx.size(), static_cast<size_t>(max_len - 1)));
    if (n > 0) std::memcpy(out_buf, tx.data(), static_cast<size_t>(n));
    out_buf[n] = '\0';
    return n;
}

VCYD_EXPORT void vcyd_set_touch_screen_px(int down, int sx, int sy) {
    if (!down) {
        vcyd::VirtualHw::instance().setTouchRaw(false, 2048, 2048);
        return;
    }
    TouchCalData cal{};
    uint8_t rot = 1;
    {
        std::lock_guard<std::mutex> lk(g_apiMtx);
        cal = g_store.settings.touchCal;
        rot = g_store.settings.rotation;
    }
    if (cal.xMax == cal.xMin || cal.yMax == cal.yMin) {
        cal.xMin = 3780; cal.xMax = 350;
        cal.yMin = 150;  cal.yMax = 3700;
        cal.swapXY = false;
    }
    int cx = max(0, min(319, sx));
    int cy = max(0, min(239, sy));

    int lx = (rot == 3) ? (319 - cx) : cx;
    int ly = (rot == 3) ? (239 - cy) : cy;

    long ax = static_cast<long>(cal.xMin) + (static_cast<long>(lx) * (static_cast<long>(cal.xMax) - static_cast<long>(cal.xMin))) / 319L;
    long ay = static_cast<long>(cal.yMin) + (static_cast<long>(ly) * (static_cast<long>(cal.yMax) - static_cast<long>(cal.yMin))) / 239L;

    uint16_t rawX = static_cast<uint16_t>(constrain(cal.swapXY ? ay : ax, 0L, 4095L));
    uint16_t rawY = static_cast<uint16_t>(constrain(cal.swapXY ? ax : ay, 0L, 4095L));
    vcyd::VirtualHw::instance().setTouchRaw(true, rawX, rawY);
}

VCYD_EXPORT void vcyd_set_touch_raw(int down, int raw_x, int raw_y) {
    vcyd::VirtualHw::instance().setTouchRaw(down != 0,
                                            static_cast<uint16_t>(max(0, min(4095, raw_x))),
                                            static_cast<uint16_t>(max(0, min(4095, raw_y))));
}

VCYD_EXPORT void vcyd_set_boot_btn(int down) {
    vcyd::VirtualHw::instance().setBootPressed(down != 0);
}

VCYD_EXPORT int vcyd_get_framebuffer_rgba(uint8_t* out_buf, int buf_len) {
    if (!out_buf || buf_len < 320 * 240 * 4) return 0;
    std::lock_guard<std::mutex> lk(g_apiMtx);
    if (!g_initialized) return 0;
    std::memcpy(out_buf, g_frontRgba.data(), 320 * 240 * 4);
    return 320 * 240 * 4;
}

VCYD_EXPORT int vcyd_get_state_json(char* out_buf, int max_len) {
    if (!out_buf || max_len <= 0) return 0;
    bool inited = false;
    int vitalsSub = 0;
    {
        std::lock_guard<std::mutex> lk(g_apiMtx);
        inited = g_initialized;
        vitalsSub = static_cast<int>(g_ui.currentVitalsSub());
    }
    auto s = vcyd::VirtualHw::instance().snapshot();
    JsonDocument doc;
    doc["initialized"] = inited;
    doc["vitals_sub"] = vitalsSub;
    doc["uptime_ms"] = s.uptime_ms;
    doc["panel_type"] = s.panel_type == 1 ? "st7789" : "ili9341";
    doc["backlight_pwm"] = s.backlight_pwm;
    doc["led"]["r"] = s.led_r;
    doc["led"]["g"] = s.led_g;
    doc["led"]["b"] = s.led_b;
    doc["amp_enabled"] = s.amp_enabled;
    doc["boot_pressed"] = s.boot_pressed;
    doc["touch_down"] = s.touch_irq_active;
    doc["touch_raw_x"] = s.touch_raw_x;
    doc["touch_raw_y"] = s.touch_raw_y;
    doc["bat_mv"] = s.bat_mv;
    doc["madctl"] = s.madctl;
    doc["inverted"] = s.inverted;
    doc["rotation"] = s.rotation;
    doc["screen"] = s.screen_name.c_str();
    doc["page"] = s.page_idx;
    doc["sleeping"] = s.sleeping;
    doc["calibrating"] = s.calibrating;
    doc["wifi_connected"] = s.wifi_connected;
    doc["wifi_ssid"] = s.wifi_ssid.c_str();
    doc["wifi_ip"] = s.wifi_ip.c_str();

    std::string out;
    serializeJson(doc, out);
    int n = static_cast<int>(min<size_t>(out.size(), static_cast<size_t>(max_len - 1)));
    std::memcpy(out_buf, out.data(), static_cast<size_t>(n));
    out_buf[n] = '\0';
    return n;
}

VCYD_EXPORT int vcyd_pop_audio_json(char* out_buf, int max_len) {
    if (!out_buf || max_len <= 0) return 0;
    auto evs = vcyd::VirtualHw::instance().popAudioEvents();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& ev : evs) {
        JsonObject o = arr.add<JsonObject>();
        o["ts_ms"] = ev.ts_ms;
        o["freq_hz"] = ev.freq_hz;
        o["ms"] = ev.duration_ms;
        o["amp"] = ev.amp_enabled;
    }
    std::string out;
    serializeJson(doc, out);
    int n = static_cast<int>(min<size_t>(out.size(), static_cast<size_t>(max_len - 1)));
    std::memcpy(out_buf, out.data(), static_cast<size_t>(n));
    out_buf[n] = '\0';
    return n;
}
