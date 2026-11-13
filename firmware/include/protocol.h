#pragma once
#include <Arduino.h>

static constexpr int MAX_DECK_PAGES = 6;
static constexpr int TILES_PER_PAGE = 6;
static constexpr int TELEM_HISTORY  = 48;
static constexpr int MAX_CORES_UI   = 16;

enum AppScreen : uint8_t {
    SCR_DECK   = 0,
    SCR_VITALS = 1,
    SCR_MEDIA  = 2,
    SCR_TIMER  = 3,
    SCR_SYS    = 4
};

enum VitalsSubView : uint8_t {
    VIT_OVERVIEW = 0,
    VIT_CPU      = 1,
    VIT_GPU      = 2,
    VIT_RAM      = 3
};

enum LedMode : uint8_t {
    LED_VITALS = 0,
    LED_CYCLE  = 1,
    LED_CYAN   = 2,
    LED_OFF    = 3
};

struct DeckTile {
    char label[18];
    char sub[14];
    char subOn[14];
    char icon[16];
    char dyn[8];   // Semantic role: "", "mic", "play", "mute"
    uint16_t color565;
    bool isToggle;
    bool isOn;
    int8_t jumpPage; // >= 0 if local folder/page jump, -1 for PC macro
    bool hasHold;
    bool holdRepeat;
};

struct DeckPage {
    char name[14];
    DeckTile tiles[TILES_PER_PAGE];
};

struct TelemetryState {
    bool connected;
    uint32_t lastPacketMs;
    char hostName[24];
    char cpuName[24];
    char gpuName[28];

    // CPU
    uint8_t  cpuPct;
    uint16_t cpuClkMhz;
    int16_t  cpuTempC;      // -1 if unavailable
    float    cpuPowerW;     // 0 if unavailable
    uint8_t  coreCount;
    uint8_t  logicalCores;
    uint8_t  coreLoads[MAX_CORES_UI];
    char     topCpuProc[20];

    // GPU
    bool     gpuAvailable;
    uint8_t  gpuPct;
    int16_t  gpuTempC;
    uint16_t gpuPowerW;
    uint16_t gpuPowerMaxW;
    uint16_t gpuVramUsedMb;
    uint16_t gpuVramTotalMb;
    uint16_t gpuCoreClkMhz;
    uint16_t gpuMemClkMhz;
    uint8_t  gpuFanPct;

    // RAM & System
    uint8_t  ramPct;
    float    ramUsedGb;
    float    ramTotalGb;
    float    commitUsedGb;
    float    commitTotalGb;
    float    netDownMbps;
    float    netUpMbps;
    float    diskFreeGb;
    float    diskReadMbps;
    float    diskWriteMbps;
    char     topRamProc[20];
    bool     togglesChanged;

    // Media & Clock
    uint8_t  volumePct;
    bool     volMuted;
    bool     micMuted;
    bool     mediaPlaying;
    char     nowPlaying[42];
    char     clockStr[8];

    // Rolling sparkline history (0..100)
    uint8_t  histCpu[TELEM_HISTORY];
    uint8_t  histGpu[TELEM_HISTORY];
    uint8_t  histRam[TELEM_HISTORY];
    uint8_t  histHead;
};

struct TouchCalData {
    uint16_t xMin;
    uint16_t xMax;
    uint16_t yMin;
    uint16_t yMax;
    bool     swapXY;
    bool     calibrated;
};

struct DeviceSettings {
    uint8_t brightness;   // 25..255
    uint8_t rotation;     // 1 or 3 (landscape)
    bool    invertColors;
    bool    swapRB;
    LedMode ledMode;
    bool    sfxEnabled;
    uint16_t sleepSec;    // 0 = never, 60, 300, 900
    TouchCalData touchCal;
};

inline uint16_t hexToRgb565(const char* hex, uint16_t fallback) {
    if (!hex) return fallback;
    if (hex[0] == '#') hex++;
    if (strlen(hex) < 6) return fallback;
    char buf[7];
    memcpy(buf, hex, 6);
    buf[6] = '\0';
    uint32_t v = strtoul(buf, nullptr, 16);
    uint8_t r = (v >> 16) & 0xFF;
    uint8_t g = (v >> 8) & 0xFF;
    uint8_t b = v & 0xFF;
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

