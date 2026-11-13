#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "protocol.h"
#include "deck_store.h"
#include "touch_input.h"

enum class UiActionType : uint8_t {
    NONE = 0,
    SWITCH_SCREEN,
    SWITCH_PAGE,
    TILE_TAP,
    TILE_HOLD,
    TILE_REPEAT,
    MEDIA_CMD,
    VOL_SET,
    MIC_TOGGLE,
    SETTING_CHANGED,
    RUN_TOUCH_CAL,
    ENTER_SLEEP,
    TIMER_ALARM
};

struct UiAction {
    UiActionType type = UiActionType::NONE;
    uint8_t arg0 = 0;
    uint8_t arg1 = 0;
    int16_t val  = 0;
    char text[24]{};
};

class DisplayUI {
public:
    void begin(const DeviceSettings& cfg);
    void applyDisplaySettings(const DeviceSettings& cfg);
    void showBootSplash();

    void markDirty();
    void markVitalsDirty(bool toggleChanged = false);
    void showToast(const char* msg, uint16_t accentColor, uint16_t durationMs = 1500);
    void applyTimerCommand(const char* cmd, uint32_t sec = 0);

    UiAction handleTouch(const TouchEvent& ev, DeckStore& store);
    void render(DeckStore& store);

    AppScreen currentScreen() const { return _screen; }
    uint8_t currentPage() const { return _page; }
    VitalsSubView currentVitalsSub() const { return _vitalsSub; }
    void setScreen(AppScreen scr);
    void setPage(uint8_t p, uint8_t pageCount);
    void setVitalsSub(VitalsSubView sub);
    void setPressedTile(int8_t idx, uint16_t untilMs = 170);

    bool timerRunning() const { return _tmrRunning; }
    bool consumeTimerAlarm();
    void tickTimer();
    TFT_eSPI& tft() { return _tft; }

private:
    TFT_eSPI _tft;
    TFT_eSprite _spr{&_tft}; // Single reusable 160x152 16-bit sprite (48.6 KB, zero fragmentation)
    int16_t _sprH = 152;

    AppScreen _screen = SCR_DECK;
    VitalsSubView _vitalsSub = VIT_OVERVIEW;
    uint8_t _page = 0;

    bool _dirtyFull = true;
    bool _dirtyBody = true;
    bool _dirtyTop  = true;
    bool _dirtyDock = true;
    bool _dirtyTimerRing = false;
    bool _dirtyEq = false;
    bool _dirtyVolSlider = false;
    bool _dirtyBlSlider = false;
    uint8_t _dirtyTileMask = 0;

    int8_t _pressedTile = -1;
    uint32_t _pressUntilMs = 0;
    int8_t _holdTile = -1;
    uint8_t _holdProgress = 0;
    bool _holdRepeatActive = false;
    uint32_t _lastRepeatMs = 0;
    int8_t _activeSlider = 0; // 0=none, 1=media_vol, 2=sys_bl

    char _toastMsg[28]{};
    uint16_t _toastColor = 0;
    uint32_t _toastUntilMs = 0;
    bool _toastWasActive = false;

    // Timer & touch confirmation/drag state (member-scoped so Virtual CYD reset clears cleanly)
    bool _tmrStopwatch = false;
    bool _tmrRunning = false;
    uint32_t _tmrTargetSec = 1500; // 25:00 default Pomodoro
    uint32_t _tmrElapsedMs = 0;
    uint32_t _tmrLastTickMs = 0;
    uint32_t _lastTimerSec = 0xFFFFFFFFUL;
    bool _tmrAlarmPending = false;
    uint32_t _tmrLastAnimMs = 0;
    uint32_t _lastAnimMs = 0;
    uint32_t _lastDragSendMs = 0;
    uint8_t  _lastDragPct = 255;
    uint32_t _presConfirmMs = 0;
    int8_t   _presConfirmIdx = -1;
    uint32_t _resetConfirmMs = 0;

    void drawTopBar(const DeckStore& store);
    void drawBottomDock();
    void drawDeckScreen(const DeckStore& store);
    void drawDeckTile(int idx, const DeckTile& tile, bool pressed, uint8_t holdPct = 0);
    void drawVitalsScreen(const DeckStore& store);
    void drawVitalsOverview(const DeckStore& store);
    void drawVitalsCpu(const DeckStore& store);
    void drawVitalsGpu(const DeckStore& store);
    void drawVitalsRam(const DeckStore& store);
    void drawMediaScreen(const DeckStore& store);
    void drawMediaVolCard(const DeckStore& store);
    void drawMediaEqualizer(const DeckStore& store);
    void drawTimerScreen();
    void drawTimerRingCard();
    void drawSysScreen(const DeckStore& store);
    void drawSysBacklightCard(const DeckStore& store);

    void drawGaugeCard(int16_t x, int16_t y, int16_t w, int16_t h,
                       const char* title, const char* corner,
                       uint8_t pct, const char* centerSub, const char* footer,
                       uint16_t baseColor);

    void fitTextToWidth(const char* src, int16_t maxW, uint8_t font, char* out, size_t outSz);
    static uint16_t loadColor(uint8_t pct, uint16_t base);
    static void tileRect(int idx, int16_t& x, int16_t& y, int16_t& w, int16_t& h);
};
