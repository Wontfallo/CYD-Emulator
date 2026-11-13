#include "deck_store.h"
#include "ui_theme.h"
#include <LittleFS.h>
#include <Preferences.h>

static Preferences s_prefs;
static constexpr const char* DECK_JSON_PATH = "/deck.json";

void DeckStore::setTile(DeckTile& t, const char* lbl, const char* sub, const char* ico,
                        uint16_t col, bool tog, bool on, int8_t jump) {
    strlcpy(t.label, lbl ? lbl : "", sizeof(t.label));
    strlcpy(t.sub, sub ? sub : "", sizeof(t.sub));
    t.subOn[0] = '\0';
    strlcpy(t.icon, ico ? ico : "bolt", sizeof(t.icon));
    t.dyn[0] = '\0';
    t.color565 = col;
    t.isToggle = tog;
    t.isOn = on;
    t.jumpPage = jump;
    t.hasHold = false;
    t.holdRepeat = false;
}

void DeckStore::begin() {
    LittleFS.begin(true);
    loadSettings();
    loadDefaults();
    loadDeckFromFS();

    // Clean initial telemetry state
    memset(&telem, 0, sizeof(telem));
    telem.connected = false;
    telem.cpuTempC = -1;
    telem.gpuTempC = -1;
    strlcpy(telem.hostName, "LINK WAIT", sizeof(telem.hostName));
    strlcpy(telem.cpuName, "PC CPU", sizeof(telem.cpuName));
    strlcpy(telem.gpuName, "PC GPU", sizeof(telem.gpuName));
    strlcpy(telem.clockStr, "--:--", sizeof(telem.clockStr));
    strlcpy(telem.nowPlaying, "Waiting for PC link...", sizeof(telem.nowPlaying));
    strlcpy(telem.topCpuProc, "-", sizeof(telem.topCpuProc));
    strlcpy(telem.topRamProc, "-", sizeof(telem.topRamProc));
    telem.volumePct = 50;
}

void DeckStore::loadDefaults() {
    pageCount = 4;

    // Page 0: MAIN
    strlcpy(pages[0].name, "MAIN", sizeof(pages[0].name));
    setTile(pages[0].tiles[0], "MIC MUTE",   "LIVE",      "mic",      Theme::CRIMSON, true,  false, -1);
    strlcpy(pages[0].tiles[0].subOn, "MUTED", sizeof(pages[0].tiles[0].subOn));
    strlcpy(pages[0].tiles[0].dyn, "mic", sizeof(pages[0].tiles[0].dyn));
    pages[0].tiles[0].hasHold = true;
    setTile(pages[0].tiles[1], "SCREENSHOT", "WIN+SHF+S", "shot",     Theme::CYAN,    false, false, -1);
    pages[0].tiles[1].hasHold = true;
    setTile(pages[0].tiles[2], "PLAY/PAUSE", "PAUSED",    "play",     Theme::EMERALD, true,  false, -1);
    strlcpy(pages[0].tiles[2].subOn, "PLAYING", sizeof(pages[0].tiles[2].subOn));
    strlcpy(pages[0].tiles[2].dyn, "play", sizeof(pages[0].tiles[2].dyn));
    pages[0].tiles[2].hasHold = true;
    setTile(pages[0].tiles[3], "VOL DOWN",   "-6%",       "vol_dn",   Theme::BLUE,    false, false, -1);
    pages[0].tiles[3].holdRepeat = true;
    setTile(pages[0].tiles[4], "VOL UP",     "+6%",       "vol_up",   Theme::BLUE,    false, false, -1);
    pages[0].tiles[4].holdRepeat = true;
    setTile(pages[0].tiles[5], "DEV LAB",    "FOLDER",    "folder",   Theme::VIOLET,  false, false, 1);

    // Page 1: DEV LAB
    strlcpy(pages[1].name, "DEV LAB", sizeof(pages[1].name));
    setTile(pages[1].tiles[0], "TERMINAL",   "WT / PS",   "terminal", Theme::CYAN,       false, false, -1);
    pages[1].tiles[0].hasHold = true;
    setTile(pages[1].tiles[1], "VS CODE",    "LAUNCH",    "code",     Theme::BLUE,       false, false, -1);
    setTile(pages[1].tiles[2], "GIT STATUS", "MACRO",     "git",      Theme::AMBER,      false, false, -1);
    setTile(pages[1].tiles[3], "CLIPBOARD",  "WIN+V",     "clipboard",Theme::VIOLET,     false, false, -1);
    setTile(pages[1].tiles[4], "BUILD RUN",  "TASK",      "rocket",   Theme::EMERALD,    false, false, -1);
    setTile(pages[1].tiles[5], "BACK MAIN",  "RETURN",    "back",     Theme::TEXT_MUTED, false, false, 0);

    // Page 2: STREAM
    strlcpy(pages[2].name, "STREAM", sizeof(pages[2].name));
    setTile(pages[2].tiles[0], "REC TOGGLE", "WIN+ALT+R", "rec",      Theme::CRIMSON,    true,  false, -1);
    setTile(pages[2].tiles[1], "CAM SCENE",  "SWITCH",    "cam",      Theme::VIOLET,     true,  false, -1);
    setTile(pages[2].tiles[2], "DISCORD",    "DEAFEN",    "discord",  Theme::BLUE,       true,  false, -1);
    setTile(pages[2].tiles[3], "CLIP THAT",  "INSTANT",   "flame",    Theme::AMBER,      false, false, -1);
    setTile(pages[2].tiles[4], "HYPE HOOK",  "WEBHOOK",   "webhook",  Theme::CYAN,       false, false, -1);
    setTile(pages[2].tiles[5], "BACK MAIN",  "RETURN",    "back",     Theme::TEXT_MUTED, false, false, 0);

    // Page 3: SYSTEM
    strlcpy(pages[3].name, "SYSTEM", sizeof(pages[3].name));
    setTile(pages[3].tiles[0], "BROWSER",    "WEB",       "browser",  Theme::CYAN,    false, false, -1);
    setTile(pages[3].tiles[1], "SNAP LEFT",  "WIN+LEFT",  "window",   Theme::BLUE,    false, false, -1);
    setTile(pages[3].tiles[2], "SNAP RGHT",  "WIN+RGHT",  "window",   Theme::BLUE,    false, false, -1);
    setTile(pages[3].tiles[3], "TASK MGR",   "SYS MON",   "cpu",      Theme::AMBER,   false, false, -1);
    setTile(pages[3].tiles[4], "SPOTIFY",    "MUSIC",     "music",    Theme::EMERALD, false, false, -1);
    setTile(pages[3].tiles[5], "LOCK PC",    "WIN+L",     "lock",     Theme::CRIMSON, false, false, -1);
}

void DeckStore::loadSettings() {
    s_prefs.begin("hydro_cfg", true);
    settings.brightness   = s_prefs.getUChar("bl", 230);
    settings.rotation     = s_prefs.getUChar("rot", 1);
    settings.invertColors = s_prefs.getBool("inv", false);
    settings.swapRB       = s_prefs.getBool("bgr", false);
    settings.ledMode      = (LedMode)s_prefs.getUChar("led", LED_VITALS);
    settings.sfxEnabled   = s_prefs.getBool("sfx", true);
    settings.sleepSec     = s_prefs.getUShort("slp", 300);

    // Default E32R28T touch mapping for the landscape UI (rotation 1, USB on the right).
    // On this panel the XPT2046 channel read with 0x91 runs along the 320 px side and the
    // one read with 0xD1 along the 240 px side, so the axes are swapped relative to
    // readRawPoint()'s rawX (0xD1) / rawY (0x91):
    //   screen X 0..319  <- 0x91 channel  493..3398
    //   screen Y 0..239  <- 0xD1 channel  706..3446
    // Ranges are the vendor's calibration for this board (Freenove Sketch_12.1:
    // touch.setCal(493, 3398, 706, 3446, 320, 240, 1)). Individual panels vary by a few
    // percent; SYS > CALIBRATE stores exact values.
    settings.touchCal.xMin       = s_prefs.getUShort("tx0", 493);
    settings.touchCal.xMax       = s_prefs.getUShort("tx1", 3398);
    settings.touchCal.yMin       = s_prefs.getUShort("ty0", 706);
    settings.touchCal.yMax       = s_prefs.getUShort("ty1", 3446);
    settings.touchCal.swapXY     = s_prefs.getBool("tsw", true);
    settings.touchCal.calibrated = s_prefs.getBool("tcal", true);
    s_prefs.end();
}

void DeckStore::saveSettings() {
    s_prefs.begin("hydro_cfg", false);
    s_prefs.putUChar("bl", settings.brightness);
    s_prefs.putUChar("rot", settings.rotation);
    s_prefs.putBool("inv", settings.invertColors);
    s_prefs.putBool("bgr", settings.swapRB);
    s_prefs.putUChar("led", (uint8_t)settings.ledMode);
    s_prefs.putBool("sfx", settings.sfxEnabled);
    s_prefs.putUShort("slp", settings.sleepSec);

    s_prefs.putUShort("tx0", settings.touchCal.xMin);
    s_prefs.putUShort("tx1", settings.touchCal.xMax);
    s_prefs.putUShort("ty0", settings.touchCal.yMin);
    s_prefs.putUShort("ty1", settings.touchCal.yMax);
    s_prefs.putBool("tsw", settings.touchCal.swapXY);
    s_prefs.putBool("tcal", settings.touchCal.calibrated);
    s_prefs.end();
}

bool DeckStore::saveDeckToFS() {
    JsonDocument doc;
    JsonArray arr = doc["pages"].to<JsonArray>();
    for (int p = 0; p < pageCount; p++) {
        JsonObject po = arr.add<JsonObject>();
        po["name"] = pages[p].name;
        JsonArray ta = po["tiles"].to<JsonArray>();
        for (int t = 0; t < TILES_PER_PAGE; t++) {
            JsonObject to = ta.add<JsonObject>();
            to["label"] = pages[p].tiles[t].label;
            to["sub"]   = pages[p].tiles[t].sub;
            to["sub2"]  = pages[p].tiles[t].subOn;
            to["icon"]  = pages[p].tiles[t].icon;
            to["dyn"]   = pages[p].tiles[t].dyn;
            to["c"]     = pages[p].tiles[t].color565;
            to["tog"]   = pages[p].tiles[t].isToggle;
            to["on"]    = pages[p].tiles[t].isOn;
            to["jmp"]   = pages[p].tiles[t].jumpPage;
            to["hh"]    = pages[p].tiles[t].hasHold;
            to["hr"]    = pages[p].tiles[t].holdRepeat;
        }
    }
    File f = LittleFS.open(DECK_JSON_PATH, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

bool DeckStore::loadDeckFromFS() {
    if (!LittleFS.exists(DECK_JSON_PATH)) return false;
    File f = LittleFS.open(DECK_JSON_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc["pages"].is<JsonArray>()) return false;

    JsonArray arr = doc["pages"].as<JsonArray>();
    int count = min<int>((int)arr.size(), MAX_DECK_PAGES);
    if (count <= 0) return false;
    pageCount = count;

    for (int p = 0; p < count; p++) {
        JsonObject po = arr[p];
        strlcpy(pages[p].name, po["name"] | "PAGE", sizeof(pages[p].name));
        JsonArray ta = po["tiles"];
        for (int t = 0; t < TILES_PER_PAGE; t++) {
            if (t < (int)ta.size()) {
                JsonObject to = ta[t];
                strlcpy(pages[p].tiles[t].label, to["label"] | "", sizeof(pages[p].tiles[t].label));
                strlcpy(pages[p].tiles[t].sub,   to["sub"]   | "", sizeof(pages[p].tiles[t].sub));
                strlcpy(pages[p].tiles[t].subOn, to["sub2"]  | "", sizeof(pages[p].tiles[t].subOn));
                strlcpy(pages[p].tiles[t].icon,  to["icon"]  | "bolt", sizeof(pages[p].tiles[t].icon));
                strlcpy(pages[p].tiles[t].dyn,   to["dyn"]   | "", sizeof(pages[p].tiles[t].dyn));
                if (to["c"].is<uint16_t>()) {
                    pages[p].tiles[t].color565 = to["c"].as<uint16_t>();
                } else {
                    pages[p].tiles[t].color565 = hexToRgb565(to["col"] | "#00F0DA", Theme::CYAN);
                }
                pages[p].tiles[t].isToggle   = to["tog"] | false;
                pages[p].tiles[t].isOn       = to["on"]  | false;
                pages[p].tiles[t].jumpPage   = to["jmp"] | -1;
                pages[p].tiles[t].hasHold    = to["hh"]  | false;
                pages[p].tiles[t].holdRepeat = to["hr"]  | false;
            } else {
                setTile(pages[p].tiles[t], "", "", "bolt", Theme::BORDER_SUB, false, false, -1);
            }
        }
    }
    return true;
}

bool DeckStore::applyDeckJson(JsonArrayConst pagesArr) {
    int count = min<int>((int)pagesArr.size(), MAX_DECK_PAGES);
    if (count <= 0) return false;
    pageCount = count;
    for (int p = 0; p < count; p++) {
        JsonObjectConst po = pagesArr[p];
        strlcpy(pages[p].name, po["name"] | "PAGE", sizeof(pages[p].name));
        JsonArrayConst ta = po["tiles"];
        for (int t = 0; t < TILES_PER_PAGE; t++) {
            if (t < (int)ta.size()) {
                JsonObjectConst to = ta[t];
                strlcpy(pages[p].tiles[t].label, to["label"] | "", sizeof(pages[p].tiles[t].label));
                strlcpy(pages[p].tiles[t].sub,   to["sub"]   | "", sizeof(pages[p].tiles[t].sub));
                strlcpy(pages[p].tiles[t].subOn, to["sub2"]  | "", sizeof(pages[p].tiles[t].subOn));
                strlcpy(pages[p].tiles[t].icon,  to["icon"]  | "bolt", sizeof(pages[p].tiles[t].icon));
                strlcpy(pages[p].tiles[t].dyn,   to["dyn"]   | "", sizeof(pages[p].tiles[t].dyn));
                pages[p].tiles[t].color565   = hexToRgb565(to["col"] | "#00F0DA", Theme::CYAN);
                pages[p].tiles[t].isToggle   = to["tog"] | false;
                pages[p].tiles[t].isOn       = to["on"]  | false;
                pages[p].tiles[t].jumpPage   = to["jmp"] | -1;
                pages[p].tiles[t].hasHold    = to["hh"]  | false;
                pages[p].tiles[t].holdRepeat = to["hr"]  | false;
            } else {
                setTile(pages[p].tiles[t], "", "", "bolt", Theme::BORDER_SUB, false, false, -1);
            }
        }
    }
    saveDeckToFS();
    return true;
}

bool DeckStore::applyTileStateJson(uint8_t pageIdx, uint8_t tileIdx, bool isOn) {
    if (pageIdx >= pageCount || tileIdx >= TILES_PER_PAGE) return false;
    pages[pageIdx].tiles[tileIdx].isOn = isOn;
    return true;
}

bool DeckStore::applyTelemetryJson(JsonObjectConst doc) {
    telem.connected = true;
    telem.lastPacketMs = millis();
    telem.togglesChanged = false;

    if (doc["host"].is<const char*>()) strlcpy(telem.hostName, doc["host"], sizeof(telem.hostName));
    if (doc["cpun"].is<const char*>()) strlcpy(telem.cpuName, doc["cpun"], sizeof(telem.cpuName));
    if (doc["gpun"].is<const char*>()) strlcpy(telem.gpuName, doc["gpun"], sizeof(telem.gpuName));
    if (doc["clk"].is<const char*>())  strlcpy(telem.clockStr, doc["clk"], sizeof(telem.clockStr));

    bool hasMetrics = (!doc["cpu"].isNull() || !doc["gpu"].isNull() || !doc["ram"].isNull());

    telem.cpuPct     = constrain((int)(doc["cpu"] | telem.cpuPct), 0, 100);
    telem.cpuClkMhz  = doc["cclk"] | telem.cpuClkMhz;
    telem.cpuTempC   = doc["ctmp"].is<int>() ? doc["ctmp"].as<int16_t>() : telem.cpuTempC;
    telem.cpuPowerW  = doc["cpwr"].is<float>() || doc["cpwr"].is<int>() ? doc["cpwr"].as<float>() : telem.cpuPowerW;
    if (doc["cproc"].is<const char*>()) strlcpy(telem.topCpuProc, doc["cproc"], sizeof(telem.topCpuProc));

    if (doc["cores"].is<JsonArrayConst>()) {
        JsonArrayConst ca = doc["cores"];
        telem.coreCount = min<uint8_t>((uint8_t)ca.size(), MAX_CORES_UI);
        for (int i = 0; i < telem.coreCount; i++) {
            telem.coreLoads[i] = constrain((int)(ca[i] | 0), 0, 100);
        }
    }
    telem.logicalCores = doc["lcores"] | telem.coreCount;

    telem.gpuPct         = constrain((int)(doc["gpu"] | telem.gpuPct), 0, 100);
    telem.gpuTempC       = doc["gtmp"].is<int>() ? doc["gtmp"].as<int16_t>() : telem.gpuTempC;
    telem.gpuPowerW      = doc["gpwr"] | telem.gpuPowerW;
    telem.gpuPowerMaxW   = doc["gpmx"] | telem.gpuPowerMaxW;
    telem.gpuVramUsedMb  = doc["vram"] | telem.gpuVramUsedMb;
    telem.gpuVramTotalMb = doc["vtot"] | telem.gpuVramTotalMb;
    telem.gpuCoreClkMhz  = doc["gclk"] | telem.gpuCoreClkMhz;
    telem.gpuMemClkMhz   = doc["gmck"] | telem.gpuMemClkMhz;
    telem.gpuFanPct      = doc["gfan"] | telem.gpuFanPct;
    if (doc["gavail"].is<bool>()) {
        telem.gpuAvailable = doc["gavail"].as<bool>();
    } else if (hasMetrics) {
        telem.gpuAvailable = (telem.gpuTempC > 0 || telem.gpuVramTotalMb > 0 || telem.gpuPowerMaxW > 0);
    }

    telem.ramPct     = constrain((int)(doc["ram"] | telem.ramPct), 0, 100);
    telem.ramUsedGb  = doc["ruse"] | telem.ramUsedGb;
    telem.ramTotalGb = doc["rtot"] | telem.ramTotalGb;
    telem.commitUsedGb  = doc["cmtu"].is<float>() || doc["cmtu"].is<int>() ? doc["cmtu"].as<float>() : telem.commitUsedGb;
    telem.commitTotalGb = doc["cmtt"].is<float>() || doc["cmtt"].is<int>() ? doc["cmtt"].as<float>() : telem.commitTotalGb;
    if (doc["rproc"].is<const char*>()) strlcpy(telem.topRamProc, doc["rproc"], sizeof(telem.topRamProc));

    telem.netDownMbps   = doc["ndn"] | telem.netDownMbps;
    telem.netUpMbps     = doc["nup"] | telem.netUpMbps;
    telem.diskFreeGb    = doc["dfre"] | telem.diskFreeGb;
    telem.diskReadMbps  = doc["dr"] | telem.diskReadMbps;
    telem.diskWriteMbps = doc["dw"] | telem.diskWriteMbps;

    telem.volumePct    = constrain((int)(doc["vol"] | telem.volumePct), 0, 100);
    telem.volMuted     = doc["vmute"] | telem.volMuted;
    telem.micMuted     = doc["mmute"] | telem.micMuted;
    telem.mediaPlaying = doc["mplay"] | telem.mediaPlaying;
    if (doc["song"].is<const char*>()) strlcpy(telem.nowPlaying, doc["song"], sizeof(telem.nowPlaying));

    // Mirror OS media/mic state onto matching deck toggle tiles cleanly (semantic role first, icon fallback)
    for (int p = 0; p < pageCount; p++) {
        for (int t = 0; t < TILES_PER_PAGE; t++) {
            DeckTile& tile = pages[p].tiles[t];
            if (!tile.isToggle) continue;
            bool prev = tile.isOn;
            const char* role = tile.dyn[0] ? tile.dyn : tile.icon;
            if (strcmp(role, "mic") == 0 || strcmp(role, "mic_off") == 0) {
                tile.isOn = telem.micMuted;
            } else if (strcmp(role, "play") == 0 || strcmp(role, "pause") == 0) {
                tile.isOn = telem.mediaPlaying;
            } else if (strcmp(role, "mute") == 0) {
                tile.isOn = telem.volMuted;
            }
            if (tile.isOn != prev) {
                telem.togglesChanged = true;
            }
        }
    }

    if (hasMetrics) {
        pushTelemHistory();
    }
    return true;
}

void DeckStore::pushTelemHistory() {
    telem.histCpu[telem.histHead] = telem.cpuPct;
    telem.histGpu[telem.histHead] = telem.gpuPct;
    telem.histRam[telem.histHead] = telem.ramPct;
    telem.histHead = (telem.histHead + 1) % TELEM_HISTORY;
}
