#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

#include "board_config.h"
#include "protocol.h"
#include "deck_store.h"
#include "hw_feedback.h"
#include "touch_input.h"
#include "display_ui.h"
#include "ui_theme.h"

static DeckStore  g_store;
static HwFeedback g_hw;
static TouchInput g_touch;
static DisplayUI  g_ui;

static String g_rxLine;
static uint32_t g_lastTelemDrawMs = 0;
static uint32_t g_lastHeartbeatMs = 0;
static bool g_wifiActive = false;
static bool g_wifiConfigured = false;
static bool g_wifiRoutesBound = false;
static char g_wifiSsid[64] = "";
static char g_wifiPass[64] = "";
static uint32_t g_lastWifiAttemptMs = 0;
static char g_wifiToken[32] = "";
static bool g_pendingRemoteCal = false;
static bool g_pendingDisplayApply = false;
static uint8_t g_bootTapCount = 0;
static uint32_t g_bootLastTapMs = 0;
static WebServer g_http(8080);

static void sendEventJson(const char* type, int arg0 = -1, int arg1 = -1, const char* txt = nullptr) {
    JsonDocument doc;
    doc["t"] = type;
    if (arg0 >= 0) doc["p"] = arg0;
    if (arg1 >= 0) doc["i"] = arg1;
    if (txt && txt[0]) doc["v"] = txt;
    serializeJson(doc, Serial);
    Serial.write('\n');
}

static void sendReadyPacket() {
    JsonDocument doc;
    doc["t"] = "ready";
    doc["fw"] = "hydro-deck-1.0";
#if defined(HYDRO_PANEL_ST7789)
    doc["panel"] = "ST7789";
#else
    doc["panel"] = "ILI9341";
#endif
    doc["w"] = CYD_SCREEN_W;
    doc["h"] = CYD_SCREEN_H;
    doc["sram"] = ESP.getFreeHeap();
    doc["pages"] = g_store.pageCount;
    doc["vbat"] = roundf(g_hw.readBatteryVolts() * 100.0f) / 100.0f;
    serializeJson(doc, Serial);
    Serial.write('\n');
}

static void processCommandJson(const String& line) {
    if (line.length() < 2) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) return;

    const char* t = doc["t"] | "";
    if (strcmp(t, "stat") == 0) {
        g_store.applyTelemetryJson(doc.as<JsonObjectConst>());
        bool togChanged = g_store.telem.togglesChanged;
        uint32_t now = millis();
        if (togChanged || (now - g_lastTelemDrawMs >= 200)) {
            g_lastTelemDrawMs = now;
            g_ui.markVitalsDirty(togChanged);
        }
    } else if (strcmp(t, "tmr") == 0) {
        const char* cmd = doc["cmd"] | "focus";
        uint32_t sec = doc["sec"] | 0;
        g_ui.applyTimerCommand(cmd, sec);
        sendEventJson("ack", (int)SCR_TIMER, (int)sec, cmd);
    } else if (strcmp(t, "deck") == 0) {
        if (doc["pages"].is<JsonArrayConst>()) {
            g_store.applyDeckJson(doc["pages"].as<JsonArrayConst>());
            g_ui.setPage(doc["page"] | 0, g_store.pageCount);
            g_ui.markDirty();
            g_ui.showToast("DECK SYNCED", Theme::CYAN, 1500);
            g_hw.playSfx(SfxType::SFX_TOGGLE_ON);
            g_hw.pulseRgb(0, 240, 218, 220);
            sendEventJson("ack", g_store.pageCount, -1, "deck");
        }
    } else if (strcmp(t, "tile_state") == 0) {
        uint8_t p = doc["p"] | 0;
        uint8_t i = doc["i"] | 0;
        bool on = doc["on"] | false;
        if (g_store.applyTileStateJson(p, i, on)) {
            g_ui.markDirty();
        }
    } else if (strcmp(t, "toast") == 0) {
        const char* msg = doc["m"] | "OK";
        uint16_t col = hexToRgb565(doc["c"] | "#00F0DA", Theme::CYAN);
        g_ui.showToast(msg, col, doc["ms"] | 1400);
        if (doc["err"] | false) {
            g_hw.playSfx(SfxType::SFX_ERROR);
            g_hw.pulseRgb(255, 45, 80, 240);
        }
    } else if (strcmp(t, "page") == 0) {
        uint8_t p = doc["p"] | 0;
        if (p < g_store.pageCount) {
            g_ui.setPage(p, g_store.pageCount);
            if (!(doc["keep"] | false)) {
                g_ui.setScreen(SCR_DECK);
            }
        }
    } else if (strcmp(t, "scr") == 0) {
        uint8_t s = doc["s"] | 0;
        if (s <= SCR_SYS) {
            g_ui.setScreen((AppScreen)s);
            if (s == SCR_VITALS && doc["v"].is<int>()) {
                g_ui.setVitalsSub((VitalsSubView)constrain(doc["v"].as<int>(), 0, 3));
            }
        }
    } else if (strcmp(t, "cal") == 0) {
        g_pendingRemoteCal = true;
    } else if (strcmp(t, "sleep") == 0) {
        g_hw.playSfx(SfxType::SFX_TOGGLE_OFF);
        g_hw.sleep();
    } else if (strcmp(t, "ping") == 0) {
        sendReadyPacket();
    } else if (strcmp(t, "cfg") == 0) {
        if (doc["bl"].is<int>())  g_store.settings.brightness = constrain(doc["bl"].as<int>(), 25, 255);
        if (doc["rot"].is<int>()) g_store.settings.rotation   = doc["rot"].as<int>() == 3 ? 3 : 1;
        if (doc["inv"].is<bool>()) g_store.settings.invertColors = doc["inv"].as<bool>();
        if (doc["bgr"].is<bool>()) g_store.settings.swapRB       = doc["bgr"].as<bool>();
        if (doc["led"].is<int>()) g_store.settings.ledMode    = (LedMode)constrain(doc["led"].as<int>(), 0, 3);
        if (doc["sfx"].is<bool>()) g_store.settings.sfxEnabled = doc["sfx"].as<bool>();
        if (doc["slp"].is<int>()) g_store.settings.sleepSec   = (uint16_t)constrain(doc["slp"].as<int>(), 0, 3600);
        g_store.saveSettings();
        g_hw.applySettings(g_store.settings);
        g_touch.setRotation(g_store.settings.rotation);
        if (g_touch.isCalibrating()) {
            // Defer panel rotation / full clear until active 4-corner calibration finishes
            g_pendingDisplayApply = true;
        } else {
            g_ui.applyDisplaySettings(g_store.settings);
            g_ui.showToast("CFG SAVED", Theme::EMERALD, 1200);
        }
        sendEventJson("ack", g_store.settings.brightness, g_store.settings.rotation, "cfg");
    }
}

static void bindWifiHttpRoutes() {
    if (g_wifiRoutesBound) return;
    g_wifiRoutesBound = true;
    g_http.on("/cmd", HTTP_POST, []() {
        if (g_wifiToken[0] && g_http.arg("token") != g_wifiToken) {
            g_http.send(401, "application/json", "{\"ok\":false,\"err\":\"unauthorized\"}");
            return;
        }
        if (g_http.hasArg("plain")) {
            processCommandJson(g_http.arg("plain"));
        }
        g_http.send(200, "application/json", "{\"ok\":true}");
    });
    g_http.on("/status", HTTP_GET, []() {
        JsonDocument st;
        st["ip"] = WiFi.localIP().toString();
        st["scr"] = (int)g_ui.currentScreen();
        st["page"] = g_ui.currentPage();
        st["sram"] = ESP.getFreeHeap();
        st["vbat"] = roundf(g_hw.readBatteryVolts() * 100.0f) / 100.0f;
        String out;
        serializeJson(st, out);
        g_http.send(200, "application/json", out);
    });
    g_http.begin();
}

static void initOptionalWifi() {
    if (!LittleFS.exists("/wifi.json")) return;
    File f = LittleFS.open("/wifi.json", "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    strlcpy(g_wifiSsid, ssid, sizeof(g_wifiSsid));
    strlcpy(g_wifiPass, pass, sizeof(g_wifiPass));
    strlcpy(g_wifiToken, doc["token"] | "", sizeof(g_wifiToken));
    if (!g_wifiSsid[0]) return;

    g_wifiConfigured = true;
    g_lastWifiAttemptMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid, g_wifiPass);
}

static void tickOptionalWifi(uint32_t now) {
    if (!g_wifiConfigured) return;
    if (WiFi.status() == WL_CONNECTED) {
        if (!g_wifiActive) {
            g_wifiActive = true;
            bindWifiHttpRoutes();
        }
        g_http.handleClient();
    } else {
        g_wifiActive = false;
        if (now - g_lastWifiAttemptMs >= 15000UL) {
            g_lastWifiAttemptMs = now;
            WiFi.begin(g_wifiSsid, g_wifiPass);
        }
    }
}

static void pollSerialInput() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            g_rxLine.trim();
            if (g_rxLine.length() > 0) {
                processCommandJson(g_rxLine);
            }
            g_rxLine = "";
        } else if (c != '\r') {
            if (g_rxLine.length() < 8192) {
                g_rxLine += c;
            }
        }
    }
}

void setup() {
    Serial.setRxBufferSize(8192);
    Serial.begin(115200);
    delay(60);
    g_rxLine.reserve(2048);

    g_store.begin();
    g_hw.begin(g_store.settings);
    g_touch.begin(g_store.settings.touchCal, g_store.settings.rotation);
    g_ui.begin(g_store.settings);

    g_hw.playSfx(SfxType::SFX_BOOT);
    g_ui.showBootSplash();

    initOptionalWifi();
    sendReadyPacket();
}

void loop() {
    uint32_t now = millis();

    pollSerialInput();
    tickOptionalWifi(now);

    // Active non-blocking 4-corner calibration wizard
    if (g_touch.isCalibrating()) {
        g_ui.tickTimer();
        if (now - g_lastHeartbeatMs >= 3000) {
            g_lastHeartbeatMs = now;
            if (!g_store.telem.connected) sendReadyPacket();
        }
        TouchCalData newCal = g_store.settings.touchCal;
        bool finished = false;
        bool ok = false;
        g_touch.stepCalibration(g_ui.tft(), newCal, finished, ok);
        uint8_t maxLoad = max(g_store.telem.cpuPct, g_store.telem.gpuPct);
        bool isHot = (g_store.telem.gpuTempC >= 80 || g_store.telem.cpuTempC >= 85);
        g_hw.loop((float)maxLoad, isHot, g_hw.isSleeping());
        if (finished) {
            g_hw.clearBootButton();
            if (g_pendingDisplayApply) {
                g_pendingDisplayApply = false;
                g_ui.applyDisplaySettings(g_store.settings);
            }
            if (ok) {
                g_store.settings.touchCal = newCal;
                g_store.saveSettings();
                g_hw.playSfx(SfxType::SFX_TOGGLE_ON);
                g_ui.showToast("TOUCH CALIBRATED", Theme::EMERALD, 1600);
            } else {
                g_ui.showToast("CAL CANCELLED", Theme::AMBER, 1400);
            }
            g_ui.markDirty();
        }
        delay(8);
        return;
    }

    // Check link freshness (disconnect cleanly after 5s silence)
    // Signed delta: lastPacketMs is stamped inside pollSerialInput(), i.e. after `now` was
    // captured above, so it can be a millisecond or two *newer* than `now`. An unsigned
    // subtraction would wrap to ~4e9 and drop the link the moment a packet arrived.
    if (g_store.telem.connected && ((int32_t)(now - g_store.telem.lastPacketMs) > 5000)) {
        g_store.telem.connected = false;
        g_ui.markVitalsDirty();
    }

    // Periodic hello/keepalive every 3s if waiting for PC link
    if (now - g_lastHeartbeatMs >= 3000) {
        g_lastHeartbeatMs = now;
        if (!g_store.telem.connected) {
            sendReadyPacket();
        }
    }

    // Physical BOOT button (short cycles screen; triple-tap within 1.2s starts touch recalibration; long toggles sleep)
    BootBtnAction btn = g_hw.pollBootButton();
    if (btn == BootBtnAction::SHORT_PRESS) {
        g_hw.notifyActivity();
        if (now - g_bootLastTapMs <= 1200) {
            g_bootTapCount++;
        } else {
            g_bootTapCount = 1;
        }
        g_bootLastTapMs = now;
        if (g_bootTapCount >= 3) {
            g_bootTapCount = 0;
            g_hw.playSfx(SfxType::SFX_PAGE);
            g_touch.startCalibration(g_ui.tft());
            return;
        }
        g_hw.playSfx(SfxType::SFX_PAGE);
        AppScreen nxt = (AppScreen)(((int)g_ui.currentScreen() + 1) % 5);
        g_ui.setScreen(nxt);
        sendEventJson("scr", (int)nxt);
    } else if (btn == BootBtnAction::LONG_PRESS) {
        g_bootTapCount = 0;
        if (g_hw.isSleeping()) {
            g_hw.wake();
            g_hw.playSfx(SfxType::SFX_TOGGLE_ON);
        } else {
            g_hw.playSfx(SfxType::SFX_TOGGLE_OFF);
            g_hw.sleep();
        }
    }

    // Resistive touch
    TouchEvent tev = g_touch.poll();
    if (tev.isDown || tev.type != TouchEventType::NONE) {
        bool wasAsleep = g_hw.isSleeping();
        g_hw.notifyActivity();
        if (wasAsleep) {
            // Absorb first touch as wake-up
            tev.type = TouchEventType::NONE;
        }
    }

    if (tev.type != TouchEventType::NONE) {
        UiAction act = g_ui.handleTouch(tev, g_store);
        switch (act.type) {
            case UiActionType::SWITCH_SCREEN:
                g_hw.playSfx(SfxType::SFX_PAGE);
                g_hw.pulseRgb(0, 200, 255, 110);
                sendEventJson("scr", act.arg0, (act.arg0 == SCR_VITALS) ? (int)act.arg1 : -1);
                break;

            case UiActionType::SWITCH_PAGE:
                g_hw.playSfx(SfxType::SFX_PAGE);
                g_hw.pulseRgb(80, 140, 255, 110);
                sendEventJson("page", act.arg0);
                break;

            case UiActionType::TILE_TAP: {
                DeckTile& t = g_store.pages[act.arg0].tiles[act.arg1];
                if (t.isToggle) {
                    t.isOn = !t.isOn;
                    g_hw.playSfx(t.isOn ? SfxType::SFX_TOGGLE_ON : SfxType::SFX_TOGGLE_OFF);
                } else {
                    g_hw.playSfx(SfxType::SFX_TAP);
                }
                g_hw.pulseRgb(0, 240, 218, 180);
                g_ui.showToast(t.label, t.color565, 950);
                sendEventJson("tap", act.arg0, act.arg1);
                break;
            }

            case UiActionType::TILE_HOLD: {
                DeckTile& t = g_store.pages[act.arg0].tiles[act.arg1];
                g_hw.playSfx(SfxType::SFX_TOGGLE_ON);
                g_hw.pulseRgb(255, 180, 40, 220);
                g_ui.showToast(t.holdRepeat ? "REPEAT MACRO" : "HOLD MACRO", Theme::AMBER, 900);
                sendEventJson("hold", act.arg0, act.arg1);
                break;
            }

            case UiActionType::TILE_REPEAT: {
                g_hw.pulseRgb(0, 190, 255, 70);
                sendEventJson("rpt", act.arg0, act.arg1);
                break;
            }

            case UiActionType::MEDIA_CMD:
                g_hw.playSfx(SfxType::SFX_TAP);
                sendEventJson("media", -1, -1, act.text);
                break;

            case UiActionType::VOL_SET:
                g_hw.playSfx(SfxType::SFX_TAP);
                sendEventJson("vol", act.val);
                break;

            case UiActionType::MIC_TOGGLE:
                g_store.telem.micMuted = !g_store.telem.micMuted;
                for (int p = 0; p < g_store.pageCount; p++) {
                    for (int k = 0; k < TILES_PER_PAGE; k++) {
                        DeckTile& tl = g_store.pages[p].tiles[k];
                        const char* role = tl.dyn[0] ? tl.dyn : tl.icon;
                        if (tl.isToggle && (strcmp(role, "mic") == 0 || strcmp(role, "mic_off") == 0)) {
                            tl.isOn = g_store.telem.micMuted;
                        }
                    }
                }
                g_hw.playSfx(g_store.telem.micMuted ? SfxType::SFX_TOGGLE_OFF : SfxType::SFX_TOGGLE_ON);
                g_hw.pulseRgb(g_store.telem.micMuted ? 255 : 0, g_store.telem.micMuted ? 40 : 230, 90, 200);
                g_ui.showToast(g_store.telem.micMuted ? "MIC MUTED" : "MIC LIVE",
                               g_store.telem.micMuted ? Theme::CRIMSON : Theme::EMERALD, 1100);
                g_ui.markVitalsDirty(true);
                sendEventJson("mic");
                break;

            case UiActionType::SETTING_CHANGED:
                g_hw.applySettings(g_store.settings);
                g_touch.setRotation(g_store.settings.rotation);
                g_hw.playSfx(SfxType::SFX_TAP);
                {
                    JsonDocument cfgDoc;
                    cfgDoc["t"] = "cfg";
                    cfgDoc["bl"] = g_store.settings.brightness;
                    cfgDoc["rot"] = g_store.settings.rotation;
                    cfgDoc["inv"] = g_store.settings.invertColors;
                    cfgDoc["bgr"] = g_store.settings.swapRB;
                    cfgDoc["led"] = (int)g_store.settings.ledMode;
                    cfgDoc["sfx"] = g_store.settings.sfxEnabled;
                    cfgDoc["slp"] = g_store.settings.sleepSec;
                    serializeJson(cfgDoc, Serial);
                    Serial.write('\n');
                }
                break;

            case UiActionType::ENTER_SLEEP:
                g_hw.playSfx(SfxType::SFX_TOGGLE_OFF);
                g_hw.sleep();
                break;

            case UiActionType::RUN_TOUCH_CAL:
                g_hw.playSfx(SfxType::SFX_PAGE);
                g_touch.startCalibration(g_ui.tft());
                return;

            default:
                break;
        }
    }

    // Remote calibration trigger from PC Studio
    if (g_pendingRemoteCal) {
        g_pendingRemoteCal = false;
        g_hw.playSfx(SfxType::SFX_PAGE);
        g_touch.startCalibration(g_ui.tft());
        return;
    }

    // Timer tick & alarm
    g_ui.tickTimer();
    if (g_ui.consumeTimerAlarm()) {
        g_hw.playSfx(SfxType::SFX_ALARM);
        g_hw.pulseRgb(255, 180, 40, 800);
        sendEventJson("alarm");
    }

    // Hardware RGB & backlight smoothing
    uint8_t maxLoad = max(g_store.telem.cpuPct, g_store.telem.gpuPct);
    bool isHot = (g_store.telem.gpuTempC >= 80 || g_store.telem.cpuTempC >= 85);
    g_hw.loop((float)maxLoad, isHot, g_hw.isSleeping());

    // Render dirty regions
    g_ui.render(g_store);

    delay(8);
}

