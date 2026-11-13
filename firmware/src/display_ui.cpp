#include "display_ui.h"
#include "board_config.h"
#include "ui_theme.h"
#include "icons_vector.h"
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
uint16_t blendColor565(uint16_t fg, uint16_t bg, uint8_t alpha) {
    uint8_t fr = (fg >> 11) & 0x1F;
    uint8_t fg6 = (fg >> 5) & 0x3F;
    uint8_t fb = fg & 0x1F;
    uint8_t br = (bg >> 11) & 0x1F;
    uint8_t bg6 = (bg >> 5) & 0x3F;
    uint8_t bb = bg & 0x1F;
    uint8_t r = (uint8_t)((fr * alpha + br * (255 - alpha)) / 255);
    uint8_t g = (uint8_t)((fg6 * alpha + bg6 * (255 - alpha)) / 255);
    uint8_t b = (uint8_t)((fb * alpha + bb * (255 - alpha)) / 255);
    return ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
}
} // namespace

void DisplayUI::begin(const DeviceSettings& cfg) {
    _screen = SCR_DECK;
    _vitalsSub = VIT_OVERVIEW;
    _page = 0;
    _pressedTile = -1;
    _holdTile = -1;
    _holdProgress = 0;
    _holdRepeatActive = false;
    _activeSlider = 0;
    _toastWasActive = false;
    _toastMsg[0] = '\0';
    _tmrStopwatch = false;
    _tmrRunning = false;
    _tmrTargetSec = 1500;
    _tmrElapsedMs = 0;
    _lastTimerSec = 0xFFFFFFFFUL;
    _tmrAlarmPending = false;
    _lastDragSendMs = 0;
    _lastDragPct = 255;
    _presConfirmMs = 0;
    _presConfirmIdx = -1;
    _resetConfirmMs = 0;

    _tft.init();
    applyDisplaySettings(cfg);
    _tft.setSwapBytes(true);

    // 160x152 16-bit sprite = 48.6 KB; single boot allocation for flicker-free cards/rings
    _spr.setColorDepth(16);
    _spr.setSwapBytes(false);
    _sprH = 152;
    if (!_spr.createSprite(160, 152)) {
        _spr.createSprite(160, 110);
        _sprH = 110;
    }

    _tft.fillScreen(Theme::BG_VOID);
    _dirtyFull = true;
}

void DisplayUI::applyDisplaySettings(const DeviceSettings& cfg) {
    _tft.setRotation(cfg.rotation == 3 ? 3 : 1);
    _tft.invertDisplay(cfg.invertColors);

    // Explicit MADCTL 0x36 tuned per panel driver
    _tft.writecommand(0x36);
#if defined(HYDRO_PANEL_ST7789)
    // ST7789 defaults RGB (bit 3 = 0); swapRB flips BGR bit 3 ON
    uint8_t madctl = (cfg.rotation == 3) ? 0xA0 : 0x60;
    if (cfg.swapRB) madctl |= 0x08;
#else
    // ILI9341 defaults BGR (bit 3 = 1); swapRB clears BGR bit 3 OFF
    uint8_t madctl = (cfg.rotation == 3) ? 0xE0 : 0x20;
    if (!cfg.swapRB) madctl |= 0x08;
#endif
    _tft.writedata(madctl);
    _dirtyFull = true;
}

void DisplayUI::showBootSplash() {
    _tft.fillScreen(Theme::BG_VOID);
    for (int i = 0; i < 6; i++) {
        _tft.drawFastHLine(40 + i * 8, 56 + i, 240 - i * 16, i < 2 ? Theme::CYAN : Theme::BLUE);
    }
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(Theme::CYAN, Theme::BG_VOID);
    _tft.drawString("HYDRO-DECK", Theme::W / 2, 92, 4);
    _tft.setTextColor(Theme::TEXT_MUTED, Theme::BG_VOID);
    _tft.drawString("COMMAND MATRIX + LIVE VITALS", Theme::W / 2, 124, 2);

    _tft.drawRoundRect(70, 154, 180, 10, 5, Theme::BORDER_HI);
    for (int p = 0; p <= 100; p += 10) {
        int bw = (174 * p) / 100;
        _tft.fillRoundRect(73, 157, max(4, bw), 4, 2, Theme::CYAN);
        delay(22);
    }
    _tft.setTextColor(Theme::TEXT_DIM, Theme::BG_VOID);
#if defined(HYDRO_PANEL_ST7789)
    _tft.drawString("E32R28T // ST7789 // 320x240", Theme::W / 2, 188, 1);
#else
    _tft.drawString("E32R28T // ILI9341 // 320x240", Theme::W / 2, 188, 1);
#endif
    delay(240);
    _tft.fillScreen(Theme::BG_VOID);
    markDirty();
}

void DisplayUI::markDirty() {
    _dirtyFull = true;
    _dirtyBody = true;
    _dirtyTop  = true;
    _dirtyDock = true;
    _dirtyTileMask = 0x3F;
    _dirtyTimerRing = true;
}

void DisplayUI::markVitalsDirty(bool toggleChanged) {
    _dirtyTop = true;
    if (_screen == SCR_VITALS || _screen == SCR_MEDIA) {
        _dirtyBody = true;
    } else if (toggleChanged && _screen == SCR_DECK) {
        _dirtyBody = true;
        _dirtyTileMask = 0x3F;
    }
}

void DisplayUI::showToast(const char* msg, uint16_t accentColor, uint16_t durationMs) {
    strlcpy(_toastMsg, msg ? msg : "", sizeof(_toastMsg));
    _toastColor = accentColor;
    _toastUntilMs = millis() + durationMs;
    _toastWasActive = true;
    _dirtyTop = true;
}

void DisplayUI::applyTimerCommand(const char* cmd, uint32_t sec) {
    if (!cmd) cmd = "focus";
    if (_screen != SCR_TIMER) {
        setScreen(SCR_TIMER);
    }
    if (strcmp(cmd, "start") == 0) {
        if (sec > 0) {
            _tmrStopwatch = false;
            _tmrTargetSec = sec;
            _tmrElapsedMs = 0;
        }
        _tmrRunning = true;
        _tmrLastTickMs = millis();
        showToast("TIMER STARTED", Theme::EMERALD, 1100);
    } else if (strcmp(cmd, "pause") == 0) {
        _tmrRunning = false;
        showToast("TIMER PAUSED", Theme::AMBER, 1100);
    } else if (strcmp(cmd, "toggle") == 0) {
        _tmrRunning = !_tmrRunning;
        _tmrLastTickMs = millis();
        showToast(_tmrRunning ? "TIMER STARTED" : "TIMER PAUSED",
                  _tmrRunning ? Theme::EMERALD : Theme::AMBER, 1100);
    } else if (strcmp(cmd, "reset") == 0) {
        _tmrRunning = false;
        _tmrElapsedMs = 0;
        _lastTimerSec = 0xFFFFFFFFUL;
        showToast("TIMER RESET", Theme::CYAN, 1100);
    } else if (strcmp(cmd, "stopwatch") == 0) {
        _tmrRunning = false;
        _tmrStopwatch = true;
        _tmrTargetSec = 0;
        _tmrElapsedMs = 0;
        _lastTimerSec = 0xFFFFFFFFUL;
        showToast("STOPWATCH READY", Theme::CYAN, 1100);
    } else if (strcmp(cmd, "set") == 0 && sec > 0) {
        _tmrRunning = false;
        _tmrStopwatch = false;
        _tmrTargetSec = sec;
        _tmrElapsedMs = 0;
        _lastTimerSec = 0xFFFFFFFFUL;
        showToast("PRESET LOADED", Theme::CYAN, 1100);
    }
    _dirtyBody = true;
    _dirtyTimerRing = true;
}

void DisplayUI::setScreen(AppScreen scr) {
    if (_screen != scr) {
        _screen = scr;
        _holdTile = -1;
        _holdProgress = 0;
        _holdRepeatActive = false;
        _pressedTile = -1;
        _activeSlider = 0;
        _lastTimerSec = 0xFFFFFFFFUL;
        _presConfirmIdx = -1;
        _presConfirmMs = 0;
        _resetConfirmMs = 0;
        if (_toastColor != Theme::CRIMSON) {
            _toastWasActive = false;
            _toastMsg[0] = '\0';
        }
        _dirtyFull = true;
        _dirtyBody = true;
        _dirtyDock = true;
        _dirtyTop  = true;
    }
}

void DisplayUI::setPage(uint8_t p, uint8_t pageCount) {
    if (pageCount == 0) return;
    _page = p % pageCount;
    _holdTile = -1;
    _holdProgress = 0;
    _holdRepeatActive = false;
    _pressedTile = -1;
    if (_screen == SCR_DECK) {
        _dirtyBody = true;
        _dirtyTop  = true;
    }
}

void DisplayUI::setVitalsSub(VitalsSubView sub) {
    if (_vitalsSub != sub) {
        _vitalsSub = sub;
        if (_screen == SCR_VITALS) {
            _dirtyBody = true;
        }
    }
}

void DisplayUI::setPressedTile(int8_t idx, uint16_t untilMs) {
    if (_pressedTile >= 0 && _pressedTile < TILES_PER_PAGE && _pressedTile != idx) {
        _dirtyTileMask |= (1 << _pressedTile);
    }
    _pressedTile = idx;
    _pressUntilMs = millis() + untilMs;
    if (_screen == SCR_DECK && idx >= 0 && idx < TILES_PER_PAGE) {
        _dirtyTileMask |= (1 << idx);
    }
}

void DisplayUI::fitTextToWidth(const char* src, int16_t maxW, uint8_t font, char* out, size_t outSz) {
    if (!out || outSz == 0) return;
    out[0] = '\0';
    if (!src || !src[0] || maxW <= 0) return;

    strlcpy(out, src, outSz);
    if (_tft.textWidth(out, font) <= maxW) return;

    const char* ell = (maxW >= 32) ? "..." : "..";
    size_t ellLen = strlen(ell);
    size_t len = strlen(out);
    while (len > 0) {
        if (len + ellLen < outSz) {
            memcpy(out + len, ell, ellLen + 1);
            if (_tft.textWidth(out, font) <= maxW) return;
        }
        len--;
        out[len] = '\0';
    }
    strlcpy(out, ell, outSz);
}

uint16_t DisplayUI::loadColor(uint8_t pct, uint16_t base) {
    if (pct >= 90) return Theme::CRIMSON;
    if (pct >= 74) return Theme::AMBER;
    return base;
}

void DisplayUI::tileRect(int idx, int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
    int col = idx % 3;
    int row = idx / 3;
    w = 98;
    h = 71;
    x = 8 + col * (w + 5);
    y = 30 + row * (h + 5);
}

bool DisplayUI::consumeTimerAlarm() {
    if (_tmrAlarmPending) {
        _tmrAlarmPending = false;
        return true;
    }
    return false;
}

void DisplayUI::tickTimer() {
    uint32_t now = millis();
    if (_tmrRunning) {
        uint32_t dt = now - _tmrLastTickMs;
        _tmrLastTickMs = now;
        _tmrElapsedMs += dt;
        if (!_tmrStopwatch && (_tmrElapsedMs / 1000UL) >= _tmrTargetSec) {
            _tmrElapsedMs = _tmrTargetSec * 1000UL;
            _tmrRunning = false;
            _tmrAlarmPending = true;
            showToast("TIMER COMPLETE!", Theme::AMBER, 2500);
            if (_screen == SCR_TIMER) {
                _dirtyBody = true;
            }
        }
    } else {
        _tmrLastTickMs = now;
    }

    if (_screen == SCR_TIMER) {
        uint32_t dispSec = 0;
        if (_tmrStopwatch) {
            dispSec = _tmrElapsedMs / 1000UL;
        } else {
            uint32_t elSec = _tmrElapsedMs / 1000UL;
            dispSec = (elSec >= _tmrTargetSec) ? 0 : (_tmrTargetSec - elSec);
        }
        if (dispSec != _lastTimerSec) {
            _lastTimerSec = dispSec;
            _tmrLastAnimMs = now;
            _dirtyTimerRing = true;
        } else if (_tmrRunning && (now - _tmrLastAnimMs >= 250)) {
            _tmrLastAnimMs = now;
            _dirtyTimerRing = true;
        }
    }
}

UiAction DisplayUI::handleTouch(const TouchEvent& ev, DeckStore& store) {
    UiAction act;

    if (ev.type == TouchEventType::NONE) {
        return act;
    }

    // Active slider capture keeps scrubbing cleanly even if finger drifts outside slider box
    if (_activeSlider != 0) {
        uint32_t now = millis();
        bool done = (ev.type == TouchEventType::TAP || ev.type == TouchEventType::RELEASE);
        if (_activeSlider == 1 && _screen == SCR_MEDIA) {
            int pct = map(constrain(ev.x, 86, 160), 86, 160, 0, 100);
            store.telem.volumePct = (uint8_t)pct;
            _dirtyVolSlider = true;
            if (done || (abs(pct - (int)_lastDragPct) >= 4 && now - _lastDragSendMs >= 80)) {
                _lastDragSendMs = now;
                _lastDragPct = (uint8_t)pct;
                act.type = UiActionType::VOL_SET;
                act.val = pct;
            }
            if (done) _activeSlider = 0;
            return act;
        }
        if (_activeSlider == 2 && _screen == SCR_SYS) {
            uint8_t bl = (uint8_t)map(constrain(ev.x, 18, 302), 18, 302, 25, 255);
            store.settings.brightness = bl;
            _dirtyBlSlider = true;
            if (done || (abs((int)bl - (int)_lastDragPct) >= 8 && now - _lastDragSendMs >= 80)) {
                _lastDragSendMs = now;
                _lastDragPct = bl;
                if (done) store.saveSettings();
                act.type = UiActionType::SETTING_CHANGED;
            }
            if (done) _activeSlider = 0;
            return act;
        }
        if (done) _activeSlider = 0;
    }

    // Bottom Navigation Dock (y >= 206, 34px tall)
    if (ev.type == TouchEventType::TAP && ev.y >= (Theme::H - Theme::BOT_BAR_H)) {
        int tab = constrain(ev.x / 64, 0, 4);
        AppScreen nextScr = (AppScreen)tab;
        if (nextScr != _screen) {
            setScreen(nextScr);
            act.type = UiActionType::SWITCH_SCREEN;
            act.arg0 = (uint8_t)nextScr;
        }
        return act;
    }

    // Screen-specific interactions
    if (_screen == SCR_DECK) {
        if (ev.type == TouchEventType::SWIPE_LEFT) {
            setPage((_page + 1) % max<uint8_t>(1, store.pageCount), store.pageCount);
            act.type = UiActionType::SWITCH_PAGE;
            act.arg0 = _page;
            return act;
        }
        if (ev.type == TouchEventType::SWIPE_RIGHT) {
            uint8_t prev = (_page == 0) ? (store.pageCount - 1) : (_page - 1);
            setPage(prev, store.pageCount);
            act.type = UiActionType::SWITCH_PAGE;
            act.arg0 = _page;
            return act;
        }

        // Page switcher bar at y = 179..205
        if (ev.type == TouchEventType::TAP && ev.y >= 179 && ev.y < (Theme::H - Theme::BOT_BAR_H)) {
            if (ev.x < 84) {
                uint8_t prev = (_page == 0) ? (store.pageCount - 1) : (_page - 1);
                setPage(prev, store.pageCount);
            } else if (ev.x >= 236) {
                setPage((_page + 1) % max<uint8_t>(1, store.pageCount), store.pageCount);
            } else if (store.pageCount > 1) {
                int totalW = store.pageCount * 18 - 4;
                int startX = (Theme::W - totalW) / 2;
                int picked = constrain((ev.x - startX) / 18, 0, store.pageCount - 1);
                if (picked == _page) return act;
                setPage((uint8_t)picked, store.pageCount);
            }
            act.type = UiActionType::SWITCH_PAGE;
            act.arg0 = _page;
            return act;
        }

        // Release clears press/hold/repeat visual cleanly on single tile
        if (ev.type == TouchEventType::RELEASE) {
            if (_holdTile >= 0 && _holdTile < TILES_PER_PAGE) {
                _dirtyTileMask |= (1 << _holdTile);
            }
            if (_pressedTile >= 0 && _pressedTile < TILES_PER_PAGE) {
                _dirtyTileMask |= (1 << _pressedTile);
            }
            _holdTile = -1;
            _holdProgress = 0;
            _holdRepeatActive = false;
            _pressedTile = -1;
            return act;
        }

        // Check 3x2 macro tiles
        for (int i = 0; i < TILES_PER_PAGE; i++) {
            int16_t tx, ty, tw, th;
            tileRect(i, tx, ty, tw, th);
            bool inTile = (ev.x >= tx && ev.x <= tx + tw && ev.y >= ty && ev.y <= ty + th);

            if (ev.type == TouchEventType::PRESS && inTile) {
                _holdTile = i;
                _holdProgress = 0;
                _holdRepeatActive = false;
                setPressedTile(i, 900);
                return act;
            }
            if (ev.type == TouchEventType::DRAG_MOVE && _holdTile == i) {
                if (_holdRepeatActive && store.pages[_page].tiles[i].holdRepeat) {
                    uint32_t now = millis();
                    if (now - _lastRepeatMs >= 145) {
                        _lastRepeatMs = now;
                        act.type = UiActionType::TILE_REPEAT;
                        act.arg0 = _page;
                        act.arg1 = (uint8_t)i;
                    }
                    return act;
                }
                const DeckTile& tile = store.pages[_page].tiles[i];
                if (ev.holdMs >= 80 && (tile.hasHold || tile.holdRepeat)) {
                    uint8_t newPct = (uint8_t)constrain(((int32_t)(ev.holdMs - 80) * 100) / 470, 5, 100);
                    if (newPct >= _holdProgress + 8 || (newPct == 100 && _holdProgress < 100)) {
                        _holdProgress = newPct;
                        _dirtyTileMask |= (1 << i);
                    }
                }
                return act;
            }
            if (inTile && ev.type == TouchEventType::TAP) {
                _holdTile = -1;
                _holdProgress = 0;
                _holdRepeatActive = false;
                const DeckTile& tile = store.pages[_page].tiles[i];
                if (tile.jumpPage >= 0 && tile.jumpPage < store.pageCount) {
                    setPage((uint8_t)tile.jumpPage, store.pageCount);
                    act.type = UiActionType::SWITCH_PAGE;
                    act.arg0 = _page;
                } else {
                    setPressedTile(i, 160);
                    act.type = UiActionType::TILE_TAP;
                    act.arg0 = _page;
                    act.arg1 = (uint8_t)i;
                }
                return act;
            }
            if (inTile && ev.type == TouchEventType::LONG_PRESS) {
                const DeckTile& tile = store.pages[_page].tiles[i];
                if (tile.holdRepeat) {
                    _holdTile = i;
                    _holdProgress = 100;
                    _holdRepeatActive = true;
                    _lastRepeatMs = millis();
                    _dirtyTileMask |= (1 << i);
                    setPressedTile(i, 900);
                } else {
                    _holdTile = -1;
                    _holdProgress = 0;
                    _holdRepeatActive = false;
                    setPressedTile(i, 220);
                }
                act.type = UiActionType::TILE_HOLD;
                act.arg0 = _page;
                act.arg1 = (uint8_t)i;
                return act;
            }
        }
        return act;
    }

    if (_screen == SCR_VITALS) {
        if (ev.type == TouchEventType::SWIPE_LEFT) {
            _vitalsSub = (VitalsSubView)(((int)_vitalsSub + 1) % 4);
            _dirtyBody = true;
            act.type = UiActionType::SWITCH_SCREEN;
            act.arg0 = (uint8_t)_screen;
            act.arg1 = (uint8_t)_vitalsSub;
            return act;
        }
        if (ev.type == TouchEventType::SWIPE_RIGHT) {
            _vitalsSub = (VitalsSubView)(((int)_vitalsSub + 3) % 4);
            _dirtyBody = true;
            act.type = UiActionType::SWITCH_SCREEN;
            act.arg0 = (uint8_t)_screen;
            act.arg1 = (uint8_t)_vitalsSub;
            return act;
        }
        if (ev.type == TouchEventType::TAP) {
            // Sub-view tabs (y=26..54)
            if (ev.y >= 26 && ev.y <= 54) {
                int sub = constrain((ev.x - 8) / 77, 0, 3);
                _vitalsSub = (VitalsSubView)sub;
                _dirtyBody = true;
                act.type = UiActionType::SWITCH_SCREEN;
                act.arg0 = (uint8_t)_screen;
                act.arg1 = (uint8_t)_vitalsSub;
                return act;
            }
            // Tap one of 3 gauge cards in overview -> zoom into CPU/GPU/RAM!
            if (_vitalsSub == VIT_OVERVIEW && ev.y >= 55 && ev.y <= 154) {
                int card = constrain((ev.x - 8) / 103, 0, 2);
                _vitalsSub = (VitalsSubView)(card + 1);
                _dirtyBody = true;
                act.type = UiActionType::SWITCH_SCREEN;
                act.arg0 = (uint8_t)_screen;
                act.arg1 = (uint8_t)_vitalsSub;
                return act;
            }
        }
        return act;
    }

    if (_screen == SCR_MEDIA) {
        if (ev.type == TouchEventType::TAP && ev.y >= 90 && ev.y <= 144) {
            if (ev.x < 108) strlcpy(act.text, "prev", sizeof(act.text));
            else if (ev.x < 212) strlcpy(act.text, "play", sizeof(act.text));
            else strlcpy(act.text, "next", sizeof(act.text));
            act.type = UiActionType::MEDIA_CMD;
            return act;
        }
        if (ev.y >= 146 && ev.y < (Theme::H - Theme::BOT_BAR_H)) {
            if (ev.type == TouchEventType::TAP && ev.x >= 210) {
                act.type = UiActionType::MIC_TOGGLE;
                return act;
            }
            if (ev.type == TouchEventType::TAP && ev.x <= 44) {
                act.type = UiActionType::MEDIA_CMD;
                strlcpy(act.text, "mute", sizeof(act.text));
                return act;
            }
            if (ev.type == TouchEventType::TAP && ev.y >= 160) {
                if (ev.x >= 45 && ev.x <= 81) {
                    int pct = max(0, (int)store.telem.volumePct - 5);
                    store.telem.volumePct = (uint8_t)pct;
                    _dirtyVolSlider = true;
                    act.type = UiActionType::VOL_SET;
                    act.val = pct;
                    return act;
                }
                if (ev.x >= 166 && ev.x <= 204) {
                    int pct = min(100, (int)store.telem.volumePct + 5);
                    store.telem.volumePct = (uint8_t)pct;
                    _dirtyVolSlider = true;
                    act.type = UiActionType::VOL_SET;
                    act.val = pct;
                    return act;
                }
            }
            if (ev.type == TouchEventType::PRESS && ev.x >= 82 && ev.x <= 165 && ev.y >= 148) {
                _activeSlider = 1;
                int pct = map(constrain(ev.x, 86, 160), 86, 160, 0, 100);
                store.telem.volumePct = (uint8_t)pct;
                _dirtyVolSlider = true;
                return act;
            }
        }
        return act;
    }

    if (_screen == SCR_TIMER && ev.type == TouchEventType::TAP) {
        if (ev.x < 150 && ev.y >= 30 && ev.y < 204) {
            _tmrRunning = !_tmrRunning;
            _tmrLastTickMs = millis();
            _dirtyBody = true;
            act.type = UiActionType::SWITCH_SCREEN;
            act.arg0 = (uint8_t)_screen;
            return act;
        }
        if (ev.x >= 152) {
            if (ev.y <= 78) {
                int p = constrain((ev.x - 156) / 40, 0, 3);
                uint32_t now = millis();
                if (_tmrRunning && (_presConfirmIdx != p || (now - _presConfirmMs) > 1500)) {
                    _presConfirmIdx = (int8_t)p;
                    _presConfirmMs = now;
                    showToast("TAP 2X TO RESET", Theme::AMBER, 1300);
                    return act;
                }
                _presConfirmIdx = -1;
                _resetConfirmMs = 0;
                _tmrRunning = false;
                _tmrElapsedMs = 0;
                if (p == 0) { _tmrStopwatch = false; _tmrTargetSec = 300; }
                else if (p == 1) { _tmrStopwatch = false; _tmrTargetSec = 900; }
                else if (p == 2) { _tmrStopwatch = false; _tmrTargetSec = 1500; }
                else { _tmrStopwatch = true; _tmrTargetSec = 0; }
                _lastTimerSec = 0xFFFFFFFFUL;
                _dirtyBody = true;
                act.type = UiActionType::SWITCH_SCREEN;
                act.arg0 = (uint8_t)_screen;
            } else if (ev.y <= 143) {
                _presConfirmIdx = -1;
                _resetConfirmMs = 0;
                _tmrRunning = !_tmrRunning;
                _tmrLastTickMs = millis();
                _dirtyBody = true;
                act.type = UiActionType::SWITCH_SCREEN;
                act.arg0 = (uint8_t)_screen;
            } else {
                uint32_t now = millis();
                if ((_tmrRunning || _tmrElapsedMs > 0) && (_resetConfirmMs == 0 || (now - _resetConfirmMs) > 1500)) {
                    _resetConfirmMs = now;
                    showToast("TAP 2X TO RESET", Theme::AMBER, 1300);
                    return act;
                }
                _presConfirmIdx = -1;
                _resetConfirmMs = 0;
                _tmrRunning = false;
                _tmrElapsedMs = 0;
                _lastTimerSec = 0xFFFFFFFFUL;
                _dirtyBody = true;
                act.type = UiActionType::SWITCH_SCREEN;
                act.arg0 = (uint8_t)_screen;
            }
        }
        return act;
    }

    if (_screen == SCR_SYS) {
        // Brightness slider (y=26..70) latches cleanly on press/tap
        if (ev.y >= 26 && ev.y <= 70 && (ev.type == TouchEventType::PRESS || ev.type == TouchEventType::TAP)) {
            if (ev.type == TouchEventType::PRESS) _activeSlider = 2;
            uint8_t bl = (uint8_t)map(constrain(ev.x, 18, 302), 18, 302, 25, 255);
            store.settings.brightness = bl;
            _dirtyBlSlider = true;
            if (ev.type == TouchEventType::TAP) {
                _activeSlider = 0;
                store.saveSettings();
                act.type = UiActionType::SETTING_CHANGED;
            }
            return act;
        }
        if (ev.type != TouchEventType::TAP) {
            return act;
        }

        // Row 0 (y=72..114): RGB | AUDIO
        if (ev.y >= 72 && ev.y <= 114) {
            if (ev.x < 160) {
                store.settings.ledMode = (LedMode)(((int)store.settings.ledMode + 1) % 4);
            } else {
                store.settings.sfxEnabled = !store.settings.sfxEnabled;
            }
        }
        // Row 1 (y=115..156): AUTO-SLEEP | ORIENTATION
        else if (ev.y >= 115 && ev.y <= 156) {
            if (ev.x < 160) {
                if (store.settings.sleepSec == 60) store.settings.sleepSec = 300;
                else if (store.settings.sleepSec == 300) store.settings.sleepSec = 900;
                else if (store.settings.sleepSec == 900) store.settings.sleepSec = 0;
                else store.settings.sleepSec = 60;
            } else {
                store.settings.rotation = (store.settings.rotation == 1) ? 3 : 1;
                applyDisplaySettings(store.settings);
            }
        }
        // Row 2 (y=157..204): PANEL FIX | SLEEP NOW | CALIBRATE
        else if (ev.y >= 157 && ev.y < (Theme::H - Theme::BOT_BAR_H)) {
            if (ev.x < 109) {
                if (!store.settings.invertColors && !store.settings.swapRB) store.settings.swapRB = true;
                else if (store.settings.swapRB && !store.settings.invertColors) store.settings.invertColors = true;
                else if (store.settings.invertColors && store.settings.swapRB) store.settings.swapRB = false;
                else { store.settings.invertColors = false; store.settings.swapRB = false; }
                applyDisplaySettings(store.settings);
            } else if (ev.x < 211) {
                act.type = UiActionType::ENTER_SLEEP;
                return act;
            } else {
                act.type = UiActionType::RUN_TOUCH_CAL;
                return act;
            }
        } else {
            return act;
        }

        store.saveSettings();
        _dirtyBody = true;
        act.type = UiActionType::SETTING_CHANGED;
        return act;
    }

    return act;
}

void DisplayUI::render(DeckStore& store) {
    uint32_t now = millis();

    // Clear single-tile tap flash without wiping whole body
    if (_pressedTile >= 0 && _holdTile < 0 && (int32_t)(now - _pressUntilMs) >= 0) {
        int8_t oldTile = _pressedTile;
        _pressedTile = -1;
        if (_screen == SCR_DECK && oldTile >= 0 && oldTile < TILES_PER_PAGE) {
            _dirtyTileMask |= (1 << oldTile);
        }
    }

    // Clear expired toast cleanly
    if (_toastWasActive && (int32_t)(now - _toastUntilMs) >= 0) {
        _toastWasActive = false;
        _dirtyTop = true;
    }

    // Smooth non-flickering animations
    if (_screen == SCR_MEDIA && store.telem.mediaPlaying) {
        if (now - _lastAnimMs >= 200) {
            _lastAnimMs = now;
            _dirtyEq = true;
        }
    }

    if (_dirtyFull) {
        _tft.fillScreen(Theme::BG_VOID);
        _dirtyFull = false;
        _dirtyTop = true;
        _dirtyBody = true;
        _dirtyDock = true;
        _dirtyTileMask = 0;
        _dirtyTimerRing = false;
    }

    if (_dirtyTop) {
        drawTopBar(store);
        _dirtyTop = false;
    }
    if (_dirtyDock) {
        drawBottomDock();
        _dirtyDock = false;
    }
    if (_dirtyBody) {
        switch (_screen) {
            case SCR_DECK:   drawDeckScreen(store);   break;
            case SCR_VITALS: drawVitalsScreen(store); break;
            case SCR_MEDIA:  drawMediaScreen(store);  break;
            case SCR_TIMER:  drawTimerScreen();       break;
            case SCR_SYS:    drawSysScreen(store);    break;
        }
        _dirtyBody = false;
        _dirtyTileMask = 0;
        _dirtyTimerRing = false;
        _dirtyEq = false;
        _dirtyVolSlider = false;
        _dirtyBlSlider = false;
        return;
    }

    // Partial updates that avoid clearing background
    if (_screen == SCR_DECK && _dirtyTileMask != 0) {
        if (_page >= store.pageCount) _page = 0;
        const DeckPage& pg = store.pages[_page];
        for (int i = 0; i < TILES_PER_PAGE; i++) {
            if (_dirtyTileMask & (1 << i)) {
                uint8_t hp = (_holdTile == i) ? _holdProgress : 0;
                drawDeckTile(i, pg.tiles[i], (_pressedTile == i), hp);
            }
        }
        _dirtyTileMask = 0;
    }
    if (_screen == SCR_TIMER && _dirtyTimerRing) {
        drawTimerRingCard();
        _dirtyTimerRing = false;
    }
    if (_screen == SCR_MEDIA && _dirtyEq) {
        drawMediaEqualizer(store);
        _dirtyEq = false;
    }
    if (_screen == SCR_MEDIA && _dirtyVolSlider) {
        drawMediaVolCard(store);
        _dirtyVolSlider = false;
    }
    if (_screen == SCR_SYS && _dirtyBlSlider) {
        drawSysBacklightCard(store);
        _dirtyBlSlider = false;
    }
}

void DisplayUI::drawTopBar(const DeckStore& store) {
    uint32_t now = millis();
    bool showToastNow = (_toastWasActive && (int32_t)(_toastUntilMs - now) > 0);

    _spr.fillRect(0, 0, 160, Theme::TOP_BAR_H, Theme::BG_DOCK);
    _spr.drawFastHLine(0, Theme::TOP_BAR_H - 1, 160, Theme::BORDER_SUB);

    uint16_t linkCol = store.telem.connected ? Theme::EMERALD : Theme::AMBER;
    _spr.fillSmoothCircle(12, 13, 4, linkCol, Theme::BG_DOCK);

    if (showToastNow) {
        char toastFit[24];
        fitTextToWidth(_toastMsg, 126, 2, toastFit, sizeof(toastFit));
        _spr.fillSmoothRoundRect(22, 3, 136, 20, 5, _toastColor, Theme::BG_DOCK);
        _spr.setTextDatum(MC_DATUM);
        _spr.setTextColor(Theme::BG_VOID, _toastColor);
        _spr.drawString(toastFit, 90, 13, 2);
    } else {
        const char* scrLabel = "HYDRO-DECK";
        if (_screen == SCR_DECK && _page < store.pageCount) {
            scrLabel = store.pages[_page].name;
        } else if (_screen == SCR_VITALS) scrLabel = "HW VITALS";
        else if (_screen == SCR_MEDIA) scrLabel = "MEDIA HUB";
        else if (_screen == SCR_TIMER) scrLabel = "CHRONO";
        else if (_screen == SCR_SYS)   scrLabel = "SYSTEM";

        char titleFit[24];
        fitTextToWidth(scrLabel, 132, 2, titleFit, sizeof(titleFit));
        _spr.setTextDatum(ML_DATUM);
        _spr.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_DOCK);
        _spr.drawString(titleFit, 22, 13, 2);
    }
    _spr.pushSprite(0, 0, 0, 0, 160, Theme::TOP_BAR_H);

    // Right half (160..319)
    _spr.fillRect(0, 0, 160, Theme::TOP_BAR_H, Theme::BG_DOCK);
    _spr.drawFastHLine(0, Theme::TOP_BAR_H - 1, 160, Theme::BORDER_SUB);
    _spr.setTextDatum(MR_DATUM);
    if (store.telem.connected) {
        char buf[24];
        snprintf(buf, sizeof(buf), "C%d%% G%d%%", store.telem.cpuPct, store.telem.gpuPct);
        _spr.setTextColor(Theme::CYAN, Theme::BG_DOCK);
        _spr.drawString(buf, 108, 13, 1);
        _spr.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_DOCK);
        _spr.drawString(store.telem.clockStr, 152, 13, 2);
    } else {
        _spr.setTextColor(Theme::AMBER, Theme::BG_DOCK);
        _spr.drawString("LINK WAIT", 152, 13, 1);
    }
    _spr.pushSprite(160, 0, 0, 0, 160, Theme::TOP_BAR_H);
}

void DisplayUI::drawBottomDock() {
    static const char* labels[5] = { "DECK", "VITALS", "MEDIA", "TIMER", "SYS" };
    int16_t y = Theme::H - Theme::BOT_BAR_H;

    for (int i = 0; i < 5; i++) {
        bool active = ((int)_screen == i);
        _spr.fillRect(0, 0, 64, Theme::BOT_BAR_H, Theme::BG_DOCK);
        _spr.drawFastHLine(0, 0, 64, Theme::BORDER_SUB);
        if (active) {
            _spr.fillSmoothRoundRect(4, 4, 56, 26, 6, Theme::BG_CARD_ALT, Theme::BG_DOCK);
            _spr.fillRect(14, 1, 36, 3, Theme::CYAN);
        }
        _spr.setTextDatum(MC_DATUM);
        _spr.setTextColor(active ? Theme::CYAN : Theme::TEXT_MUTED,
                          active ? Theme::BG_CARD_ALT : Theme::BG_DOCK);
        _spr.drawString(labels[i], 32, 17, active ? 2 : 1);
        _spr.pushSprite(i * 64, y, 0, 0, 64, Theme::BOT_BAR_H);
    }
}

void DisplayUI::drawDeckTile(int idx, const DeckTile& tile, bool pressed, uint8_t holdPct) {
    int16_t x, y, w, h;
    tileRect(idx, x, y, w, h);

    bool toggleOn = (tile.isToggle && tile.isOn);
    bool activeGlow = pressed || toggleOn || (holdPct > 0);
    uint16_t fillCol = Theme::BG_CARD;
    if (pressed) fillCol = Theme::BG_CARD_ALT;
    else if (toggleOn) fillCol = blendColor565(tile.color565, Theme::BG_CARD, 52);

    uint16_t rimCol = activeGlow ? tile.color565 : Theme::BORDER_SUB;
    if (holdPct >= 75) rimCol = Theme::AMBER;

    _spr.fillRect(0, 0, w, h, Theme::BG_VOID);
    _spr.fillSmoothRoundRect(0, 0, w, h, 8, rimCol, Theme::BG_VOID);
    int16_t inset = activeGlow ? 2 : 1;
    _spr.fillSmoothRoundRect(inset, inset, w - inset * 2, h - inset * 2, 7, fillCol, rimCol);

    // Top neon accent bar cleanly clears corner badges
    _spr.fillSmoothRoundRect(24, 3, w - 48, 3, 1, tile.color565, fillCol);

    // Top-left Hold [H] / Repeat [R] / Both [H+] badge
    if (tile.hasHold || tile.holdRepeat) {
        uint16_t hCol = (holdPct > 0) ? Theme::AMBER : (tile.holdRepeat ? Theme::BLUE : Theme::CYAN);
        const char* hBadge = (tile.hasHold && tile.holdRepeat) ? "H+" : (tile.holdRepeat ? "R" : "H");
        int16_t bw = (tile.hasHold && tile.holdRepeat) ? 16 : 13;
        _spr.fillSmoothRoundRect(5, 5, bw, 10, 2, Theme::TRACK_DARK, fillCol);
        _spr.setTextDatum(MC_DATUM);
        _spr.setTextColor(hCol, Theme::TRACK_DARK);
        _spr.drawString(hBadge, 5 + bw / 2, 10, 1);
    }

    // Top-right Toggle / Folder indicator
    if (tile.isToggle) {
        if (toggleOn) {
            _spr.fillSmoothCircle(w - 10, 10, 4, tile.color565, fillCol);
        } else {
            _spr.drawCircle(w - 10, 10, 4, Theme::TEXT_DIM);
        }
    } else if (tile.jumpPage >= 0) {
        _spr.setTextDatum(TR_DATUM);
        _spr.setTextColor(tile.color565, fillCol);
        _spr.drawString(">", w - 6, 5, 1);
    }

    // Centered crisp vector icon with live toggle state swap
    const char* role = tile.dyn[0] ? tile.dyn : tile.icon;
    const char* drawIcon = tile.icon;
    if (tile.isToggle && toggleOn) {
        if (strcmp(tile.icon, "mic") == 0) drawIcon = "mic_off";
        else if (strcmp(tile.icon, "play") == 0) drawIcon = "pause";
        else if (strcmp(tile.icon, "vol_up") == 0) drawIcon = "mute";
    }
    uint16_t iconFg = activeGlow ? tile.color565 : Theme::TEXT_PRIMARY;
    Icons::drawVectorIcon(_spr, drawIcon, w / 2, 22, 22, iconFg, fillCol, tile.color565);

    // Fitted Label & Sub
    char lblFit[18];
    fitTextToWidth(tile.label, w - 10, 1, lblFit, sizeof(lblFit));
    _spr.setTextDatum(MC_DATUM);
    _spr.setTextColor(Theme::TEXT_PRIMARY, fillCol);
    _spr.drawString(lblFit, w / 2, 44, 1);

    const char* rawSub = tile.sub;
    bool showingHoldPreview = (holdPct >= 15);
    if (showingHoldPreview) {
        rawSub = tile.holdRepeat ? "REPEAT..." : "HOLD...";
    } else if (tile.isToggle && toggleOn) {
        if (tile.subOn[0]) rawSub = tile.subOn;
        else rawSub = (strcmp(role, "mic") == 0 || strstr(tile.icon, "mic") != nullptr) ? "MUTED" : "ACTIVE";
    } else if (tile.isToggle && !toggleOn && !rawSub[0] && (strcmp(role, "mic") == 0 || strstr(tile.icon, "mic") != nullptr)) {
        rawSub = "LIVE";
    }
    if (rawSub[0]) {
        char subFit[18];
        fitTextToWidth(rawSub, w - 10, 1, subFit, sizeof(subFit));
        uint16_t subCol = showingHoldPreview ? Theme::AMBER : (toggleOn ? tile.color565 : Theme::TEXT_MUTED);
        _spr.setTextColor(subCol, fillCol);
        _spr.drawString(subFit, w / 2, 55, 1);
    }

    // Live hold progress bar along bottom with clear breathing room below subtext
    if (holdPct > 0) {
        _spr.fillRoundRect(7, h - 7, w - 14, 3, 1, Theme::TRACK_DARK);
        int16_t pw = (int16_t)(((w - 14) * holdPct) / 100);
        if (pw > 0) {
            _spr.fillRoundRect(7, h - 7, max<int16_t>(3, pw), 3, 1,
                               holdPct >= 95 ? Theme::EMERALD : Theme::AMBER);
        }
    }

    _spr.pushSprite(x, y, 0, 0, w, h);
}

void DisplayUI::drawDeckScreen(const DeckStore& store) {
    _tft.fillRect(0, Theme::BODY_Y, Theme::W, Theme::BODY_H, Theme::BG_VOID);

    if (_page >= store.pageCount) _page = 0;
    const DeckPage& pg = store.pages[_page];

    for (int i = 0; i < TILES_PER_PAGE; i++) {
        uint8_t hp = (_holdTile == i) ? _holdProgress : 0;
        drawDeckTile(i, pg.tiles[i], (_pressedTile == i), hp);
    }

    // Ergonomic resistive page bar at y = 180..203
    _tft.fillSmoothRoundRect(8, 180, 58, 22, 5, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(9, 181, 56, 20, 4, Theme::BG_CARD, Theme::BORDER_SUB);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
    _tft.drawString("< PREV", 37, 191, 1);

    _tft.fillSmoothRoundRect(254, 180, 58, 22, 5, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(255, 181, 56, 20, 4, Theme::BG_CARD, Theme::BORDER_SUB);
    _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
    _tft.drawString("NEXT >", 283, 191, 1);

    int totalW = store.pageCount * 18 - 4;
    int startX = (Theme::W - totalW) / 2;
    for (int p = 0; p < store.pageCount; p++) {
        int px = startX + p * 18;
        if (p == _page) {
            _tft.fillRoundRect(px, 188, 14, 6, 3, Theme::CYAN);
        } else {
            _tft.fillCircle(px + 7, 191, 3, Theme::BORDER_HI);
        }
    }
}

void DisplayUI::drawGaugeCard(int16_t x, int16_t y, int16_t w, int16_t h,
                              const char* title, const char* corner,
                              uint8_t pct, const char* centerSub, const char* footer,
                              uint16_t baseColor) {
    bool validPct = (pct <= 100);
    uint8_t clampedPct = validPct ? pct : 0;
    uint16_t col = validPct ? loadColor(clampedPct, baseColor) : Theme::TEXT_DIM;
    _spr.fillRect(0, 0, w, h, Theme::BG_VOID);
    _spr.fillSmoothRoundRect(0, 0, w, h, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _spr.fillSmoothRoundRect(1, 1, w - 2, h - 2, 7, Theme::BG_CARD, Theme::BORDER_SUB);

    // Header row
    _spr.setTextDatum(TL_DATUM);
    _spr.setTextColor(validPct ? col : Theme::TEXT_MUTED, Theme::BG_CARD);
    _spr.drawString(title, 8, 5, 1);

    char cornerFit[14];
    fitTextToWidth(corner, w / 2, 1, cornerFit, sizeof(cornerFit));
    _spr.setTextDatum(TR_DATUM);
    _spr.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
    _spr.drawString(cornerFit, w - 7, 5, 1);

    bool tallCard = (h >= 130);
    const int16_t cx = w / 2;
    const int16_t cy = tallCard ? 62 : 44;
    const int16_t r  = tallCard ? 36 : 24;
    const int16_t ir = tallCard ? 28 : 18;

    // 270 degree arc (45 deg bottom-left .. 315 deg bottom-right)
    _spr.drawSmoothArc(cx, cy, r, ir, 45, 315, Theme::TRACK_DARK, Theme::BG_CARD, true);
    if (validPct && clampedPct > 0) {
        uint16_t endDeg = 45 + (uint16_t)((270UL * clampedPct) / 100UL);
        _spr.drawSmoothArc(cx, cy, r, ir, 45, endDeg, col, Theme::BG_CARD, true);
    }

    char pctBuf[8];
    if (validPct) snprintf(pctBuf, sizeof(pctBuf), "%d%%", clampedPct);
    else strlcpy(pctBuf, "N/A", sizeof(pctBuf));
    _spr.setTextDatum(MC_DATUM);
    _spr.setTextColor(validPct ? Theme::TEXT_PRIMARY : Theme::TEXT_MUTED, Theme::BG_CARD);

    if (tallCard) {
        _spr.drawString(pctBuf, cx, cy - 6, 2);
        char subFit[14];
        fitTextToWidth(centerSub, (ir - 3) * 2, 1, subFit, sizeof(subFit));
        _spr.setTextColor(col, Theme::BG_CARD);
        _spr.drawString(subFit, cx, cy + 12, 1);
        _spr.drawFastHLine(10, h - 24, w - 20, Theme::BORDER_SUB);
    } else {
        // Compact card: keep % inside ring and place sub safely below arc opening (no overlap)
        _spr.drawString(pctBuf, cx, cy, 2);
        char subFit[16];
        fitTextToWidth(centerSub, w - 12, 1, subFit, sizeof(subFit));
        _spr.setTextColor(col, Theme::BG_CARD);
        _spr.drawString(subFit, cx, cy + r + 5, 1);
    }

    char footFit[22];
    fitTextToWidth(footer, w - 12, 1, footFit, sizeof(footFit));
    _spr.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
    _spr.drawString(footFit, cx, h - 10, 1);

    _spr.pushSprite(x, y, 0, 0, min<int16_t>(w, 160), min<int16_t>(h, _sprH));
}

void DisplayUI::drawVitalsScreen(const DeckStore& store) {
    _tft.fillRect(0, Theme::BODY_Y, Theme::W, Theme::BODY_H, Theme::BG_VOID);

    // Taller 24px sub-view selector pills (y=28..51)
    static const char* subNames[4] = { "OVERVIEW", "CPU", "GPU", "RAM" };
    static const uint16_t subCols[4] = { Theme::CYAN, Theme::BLUE, Theme::EMERALD, Theme::VIOLET };
    for (int i = 0; i < 4; i++) {
        bool act = ((int)_vitalsSub == i);
        int16_t sx = 8 + i * 77;
        uint16_t bg = act ? subCols[i] : Theme::BG_CARD;
        uint16_t fg = act ? Theme::BG_VOID : Theme::TEXT_MUTED;
        _spr.fillRect(0, 0, 73, 24, Theme::BG_VOID);
        _spr.fillSmoothRoundRect(0, 0, 73, 24, 5, act ? subCols[i] : Theme::BORDER_SUB, Theme::BG_VOID);
        _spr.fillSmoothRoundRect(1, 1, 71, 22, 4, bg, act ? subCols[i] : Theme::BORDER_SUB);
        _spr.setTextDatum(MC_DATUM);
        _spr.setTextColor(fg, bg);
        _spr.drawString(subNames[i], 36, 12, 1);
        _spr.pushSprite(sx, 28, 0, 0, 73, 24);
    }

    switch (_vitalsSub) {
        case VIT_OVERVIEW: drawVitalsOverview(store); break;
        case VIT_CPU:      drawVitalsCpu(store);      break;
        case VIT_GPU:      drawVitalsGpu(store);      break;
        case VIT_RAM:      drawVitalsRam(store);      break;
    }
}

void DisplayUI::drawVitalsOverview(const DeckStore& store) {
    const auto& t = store.telem;
    char corner[16], sub[16], foot[24];

    // CPU Card (y=56..151, h=96)
    uint8_t lcores = max<uint8_t>(t.coreCount, t.logicalCores);
    if (t.cpuTempC > 0) snprintf(corner, sizeof(corner), "%dC", t.cpuTempC);
    else snprintf(corner, sizeof(corner), "%dT", max<int>(1, lcores));
    snprintf(sub, sizeof(sub), "%.1fGHz", t.cpuClkMhz / 1000.0f);
    snprintf(foot, sizeof(foot), "%s", t.topCpuProc[0] ? t.topCpuProc : "CPU");
    drawGaugeCard(8, 56, 98, 96, "CPU", corner, t.cpuPct, sub, foot, Theme::CYAN);

    // GPU Card
    if (t.gpuTempC > 0) snprintf(corner, sizeof(corner), "%dC", t.gpuTempC);
    else strlcpy(corner, "N/A", sizeof(corner));
    if (t.gpuPowerMaxW > 0 || t.gpuPowerW > 0) snprintf(sub, sizeof(sub), "%dW", t.gpuPowerW);
    else strlcpy(sub, "N/A", sizeof(sub));
    if (t.gpuVramTotalMb > 0) {
        snprintf(foot, sizeof(foot), "%.1f/%.0fG", t.gpuVramUsedMb / 1024.0f, t.gpuVramTotalMb / 1024.0f);
    } else {
        strlcpy(foot, "iGPU / N/A", sizeof(foot));
    }
    drawGaugeCard(111, 56, 98, 96, "GPU", corner, t.gpuAvailable ? t.gpuPct : 255, sub, foot, Theme::EMERALD);

    // RAM Card
    snprintf(corner, sizeof(corner), "%.0fG", t.ramTotalGb);
    snprintf(sub, sizeof(sub), "%.1fG", t.ramUsedGb);
    snprintf(foot, sizeof(foot), "%s", t.topRamProc[0] ? t.topRamProc : "MEM");
    drawGaugeCard(214, 56, 98, 96, "RAM", corner, t.ramPct, sub, foot, Theme::VIOLET);

    // Framed Live Multi-Trace Sparkline (y=156..204, 304x49 rendered in 2 seamless halves of 152x49)
    uint16_t cpuFillCol = blendColor565(Theme::CYAN, Theme::BG_SURFACE, 48);
    for (int h = 0; h < 2; h++) {
        _spr.fillRect(0, 0, 152, 49, Theme::BG_VOID);
        _spr.fillSmoothRoundRect(0, 0, 152, 49, 7, Theme::BORDER_SUB, Theme::BG_VOID);
        _spr.fillSmoothRoundRect(1, 1, 150, 47, 6, Theme::BG_SURFACE, Theme::BORDER_SUB);
        if (h == 0) {
            _spr.fillRect(145, 0, 7, 49, Theme::BG_SURFACE);
            _spr.drawFastHLine(145, 0, 7, Theme::BORDER_SUB);
            _spr.drawFastHLine(145, 48, 7, Theme::BORDER_SUB);
        } else {
            _spr.fillRect(0, 0, 7, 49, Theme::BG_SURFACE);
            _spr.drawFastHLine(0, 0, 7, Theme::BORDER_SUB);
            _spr.drawFastHLine(0, 48, 7, Theme::BORDER_SUB);
        }

        // Y gridlines at 100%, 50%, 0% with generous top header clearance
        int gxStart = (h == 0) ? 28 : 0;
        int gxLen = (h == 0) ? 124 : 148;
        _spr.drawFastHLine(gxStart, 17, gxLen, Theme::TRACK_DARK);
        _spr.drawFastHLine(gxStart, 30, gxLen, Theme::TRACK_DARK);
        _spr.drawFastHLine(gxStart, 44, gxLen, Theme::BORDER_SUB);

        if (h == 0) {
            _spr.setTextDatum(TR_DATUM);
            _spr.setTextColor(Theme::TEXT_DIM, Theme::BG_SURFACE);
            _spr.drawString("100", 24, 13, 1);
            _spr.drawString("50", 24, 26, 1);
            _spr.drawString("0", 24, 38, 1);
        }

        int baseX = 8 + h * 152;
        int minPx = (h == 0) ? 1 : 0;
        int maxPx = (h == 0) ? 151 : 150;
        int lastLx = -1, lastYc = 0, lastYg = 0, lastYr = 0;
        for (int i = 0; i < TELEM_HISTORY - 1; i++) {
            int idx0 = (t.histHead + i) % TELEM_HISTORY;
            int idx1 = (t.histHead + i + 1) % TELEM_HISTORY;
            int gx0 = 34 + (i * 271) / (TELEM_HISTORY - 1);
            int gx1 = 34 + ((i + 1) * 271) / (TELEM_HISTORY - 1);
            int lx0 = gx0 - baseX;
            int lx1 = gx1 - baseX;
            if (lx1 < minPx || lx0 > maxPx) continue;

            int yc0 = 43 - (t.histCpu[idx0] * 26) / 100;
            int yc1 = 43 - (t.histCpu[idx1] * 26) / 100;
            int yg0 = 43 - (t.histGpu[idx0] * 26) / 100;
            int yg1 = 43 - (t.histGpu[idx1] * 26) / 100;
            int yr0 = 43 - (t.histRam[idx0] * 26) / 100;
            int yr1 = 43 - (t.histRam[idx1] * 26) / 100;

            int pxStart = max(minPx, lx0);
            int pxEnd   = min(maxPx, lx1);
            int span    = max(1, lx1 - lx0);
            for (int px = pxStart; px <= pxEnd; px++) {
                int yf = yc0 + ((yc1 - yc0) * (px - lx0)) / span;
                if (yf < 43) {
                    _spr.drawFastVLine(px, yf + 1, 43 - yf, cpuFillCol);
                }
            }
            _spr.drawLine(lx0, yr0, lx1, yr1, Theme::VIOLET);
            _spr.drawLine(lx0, yg0, lx1, yg1, Theme::EMERALD);
            _spr.drawLine(lx0, yc0, lx1, yc1, Theme::CYAN);
            if (i == TELEM_HISTORY - 2 && lx1 >= minPx && lx1 <= maxPx) {
                lastLx = lx1;
                lastYc = yc1;
                lastYg = yg1;
                lastYr = yr1;
            }
        }
        if (lastLx >= 0) {
            _spr.fillCircle(lastLx, lastYr, 2, Theme::VIOLET);
            _spr.fillCircle(lastLx, lastYg, 2, Theme::EMERALD);
            _spr.fillCircle(lastLx, lastYc, 2, Theme::CYAN);
        }

        if (h == 0) {
            _spr.setTextDatum(TL_DATUM);
            _spr.setTextColor(Theme::CYAN, Theme::BG_SURFACE);
            _spr.drawString("48s TRACE", 6, 2, 1);
        } else {
            char netStr[26];
            snprintf(netStr, sizeof(netStr), "D:%.1f U:%.1fM", t.netDownMbps, t.netUpMbps);
            _spr.setTextDatum(TR_DATUM);
            _spr.setTextColor(Theme::TEXT_MUTED, Theme::BG_SURFACE);
            _spr.drawString(netStr, 146, 2, 1);
        }
        _spr.pushSprite(8 + h * 152, 156, 0, 0, 152, 49);
    }
}

void DisplayUI::drawVitalsCpu(const DeckStore& store) {
    const auto& t = store.telem;
    char corner[16], sub[16];
    if (t.cpuTempC > 0 && t.cpuPowerW >= 1.0f) snprintf(corner, sizeof(corner), "%dC %dW", t.cpuTempC, (int)roundf(t.cpuPowerW));
    else if (t.cpuTempC > 0) snprintf(corner, sizeof(corner), "%dC", t.cpuTempC);
    else if (t.cpuPowerW >= 1.0f) snprintf(corner, sizeof(corner), "%dW", (int)roundf(t.cpuPowerW));
    else strlcpy(corner, "N/A", sizeof(corner));
    snprintf(sub, sizeof(sub), "%.2fGHz", t.cpuClkMhz / 1000.0f);
    drawGaugeCard(8, 56, 108, 148, "CPU", corner, t.cpuPct, sub, t.cpuName, Theme::BLUE);

    _tft.fillSmoothRoundRect(122, 56, 190, 148, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(123, 57, 188, 146, 7, Theme::BG_CARD, Theme::BORDER_SUB);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(Theme::CYAN, Theme::BG_CARD);
    _tft.drawString("CPU CORES", 130, 62, 1);

    int cores = max<int>(1, min<int>(t.coreCount, MAX_CORES_UI));
    uint8_t lcores = max<uint8_t>((uint8_t)cores, t.logicalCores);
    bool bucketed = (lcores > cores);
    char cCnt[22];
    if (bucketed) snprintf(cCnt, sizeof(cCnt), "%d BUCKETS (%dT)", cores, lcores);
    else snprintf(cCnt, sizeof(cCnt), "%d THREADS", cores);
    _tft.setTextDatum(TR_DATUM);
    _tft.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
    _tft.drawString(cCnt, 305, 62, 1);

    for (int i = 0; i < cores; i++) {
        int col = i / 8;
        int row = i % 8;
        int bx = 129 + col * 94;
        int by = 77 + row * 15;
        char cLbl[6], pLbl[6];
        snprintf(cLbl, sizeof(cLbl), bucketed ? "G%d" : "C%d", i);
        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
        _tft.drawString(cLbl, bx, by + 1, 1);

        int barX = bx + 21;
        int barW = 35;
        uint8_t load = t.coreLoads[i];
        int fillW = (barW * load) / 100;
        _tft.fillRoundRect(barX, by + 2, barW, 7, 3, Theme::TRACK_DARK);
        if (fillW > 0) {
            _tft.fillRoundRect(barX, by + 2, max(3, fillW), 7, 3, loadColor(load, Theme::CYAN));
        }

        snprintf(pLbl, sizeof(pLbl), "%d%%", load);
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
        _tft.drawString(pLbl, bx + 83, by + 1, 1);
    }
}

void DisplayUI::drawVitalsGpu(const DeckStore& store) {
    const auto& t = store.telem;
    char corner[16], sub[16], foot[24];
    if (t.gpuTempC > 0) snprintf(corner, sizeof(corner), "%dC", t.gpuTempC);
    else strlcpy(corner, "N/A", sizeof(corner));
    if (t.gpuPowerMaxW > 0 || t.gpuPowerW > 0) snprintf(sub, sizeof(sub), "%dW", t.gpuPowerW);
    else strlcpy(sub, "N/A", sizeof(sub));
    if (t.gpuTempC > 0) {
        if (t.gpuFanPct == 0) strlcpy(foot, "FAN IDLE", sizeof(foot));
        else snprintf(foot, sizeof(foot), "FAN %d%%", t.gpuFanPct);
    } else {
        strlcpy(foot, "FAN N/A", sizeof(foot));
    }
    drawGaugeCard(8, 56, 108, 148, "GPU", corner, t.gpuAvailable ? t.gpuPct : 255, sub, foot, Theme::EMERALD);

    _tft.fillSmoothRoundRect(122, 56, 190, 148, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(123, 57, 188, 146, 7, Theme::BG_CARD, Theme::BORDER_SUB);

    char gpuTitle[32];
    fitTextToWidth(t.gpuName[0] ? t.gpuName : "GPU Telemetry", 174, 1, gpuTitle, sizeof(gpuTitle));
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(Theme::EMERALD, Theme::BG_CARD);
    _tft.drawString(gpuTitle, 130, 62, 1);

    auto drawStatBar = [&](int16_t y, const char* lbl, const char* val, float ratio, uint16_t col) {
        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
        _tft.drawString(lbl, 130, y, 1);
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
        _tft.drawString(val, 304, y, 1);
        _tft.fillRoundRect(130, y + 11, 174, 7, 3, Theme::TRACK_DARK);
        int fw = (int)(174.0f * constrain(ratio, 0.0f, 1.0f));
        if (fw > 0) _tft.fillRoundRect(130, y + 11, max(4, fw), 7, 3, col);
    };

    char b1[24], b2[24], b3[24], b4[24];
    float pwrRatio = (t.gpuPowerMaxW > 0) ? (float)t.gpuPowerW / t.gpuPowerMaxW : 0.0f;
    if (t.gpuPowerMaxW > 0) snprintf(b1, sizeof(b1), "%d / %d W", t.gpuPowerW, t.gpuPowerMaxW);
    else strlcpy(b1, "N/A", sizeof(b1));
    drawStatBar(79, "BOARD POWER", b1, pwrRatio, Theme::AMBER);

    float vramRatio = (t.gpuVramTotalMb > 0) ? (float)t.gpuVramUsedMb / t.gpuVramTotalMb : 0.0f;
    if (t.gpuVramTotalMb > 0) snprintf(b2, sizeof(b2), "%.1f / %.1f GB", t.gpuVramUsedMb / 1024.0f, t.gpuVramTotalMb / 1024.0f);
    else strlcpy(b2, "N/A", sizeof(b2));
    drawStatBar(107, "VRAM ALLOC", b2, vramRatio, Theme::VIOLET);

    if (t.gpuCoreClkMhz > 0 || t.gpuMemClkMhz > 0) {
        snprintf(b3, sizeof(b3), "%d / %d MHz", t.gpuCoreClkMhz, t.gpuMemClkMhz);
    } else {
        strlcpy(b3, "N/A", sizeof(b3));
    }
    drawStatBar(135, "CLOCKS", b3, constrain(t.gpuCoreClkMhz / 2800.0f, 0.0f, 1.0f), Theme::CYAN);

    if (t.gpuTempC > 0) {
        if (t.gpuFanPct == 0) snprintf(b4, sizeof(b4), "%dC  ZERO-RPM", t.gpuTempC);
        else snprintf(b4, sizeof(b4), "%dC  FAN %d%%", t.gpuTempC, t.gpuFanPct);
        drawStatBar(163, "THERMAL / FAN", b4, constrain(t.gpuTempC / 95.0f, 0.0f, 1.0f),
                    loadColor((uint8_t)t.gpuTempC, Theme::EMERALD));
    } else {
        drawStatBar(163, "THERMAL / FAN", "N/A", 0.0f, Theme::TEXT_DIM);
    }
}

void DisplayUI::drawVitalsRam(const DeckStore& store) {
    const auto& t = store.telem;
    char corner[16], sub[16];
    snprintf(corner, sizeof(corner), "%.0fG", t.ramTotalGb);
    snprintf(sub, sizeof(sub), "%.1fG", t.ramUsedGb);
    drawGaugeCard(8, 56, 108, 148, "RAM", corner, t.ramPct, sub, t.topRamProc, Theme::VIOLET);

    _tft.fillSmoothRoundRect(122, 56, 190, 148, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(123, 57, 188, 146, 7, Theme::BG_CARD, Theme::BORDER_SUB);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(Theme::VIOLET, Theme::BG_CARD);
    _tft.drawString("MEMORY & SYSTEM I/O", 130, 62, 1);

    auto drawRow = [&](int16_t y, const char* k, const char* v, uint16_t vcol) {
        char valFit[26];
        fitTextToWidth(v && v[0] ? v : "--", 96, 1, valFit, sizeof(valFit));
        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
        _tft.drawString(k, 130, y, 1);
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(vcol, Theme::BG_CARD);
        _tft.drawString(valFit, 304, y, 1);
    };

    char r1[24], rc[24], r2[24], r3[24], r4[24];
    snprintf(r1, sizeof(r1), "%.1f / %.1f GB", t.ramUsedGb, t.ramTotalGb);
    drawRow(76, "USED / TOTAL", r1, Theme::TEXT_PRIMARY);
    if (t.commitTotalGb > 0.1f) snprintf(rc, sizeof(rc), "%.1f / %.0f GB", t.commitUsedGb, t.commitTotalGb);
    else strlcpy(rc, "N/A", sizeof(rc));
    drawRow(94, "COMMITTED", rc, Theme::BLUE);
    drawRow(112, "TOP RAM APP", t.topRamProc, Theme::VIOLET);
    snprintf(r2, sizeof(r2), "%.1f / %.1f M", t.netDownMbps, t.netUpMbps);
    drawRow(130, "NET DN / UP", r2, Theme::CYAN);
    snprintf(r3, sizeof(r3), "%.0f / %.0f MB/s", t.diskReadMbps, t.diskWriteMbps);
    drawRow(148, "DISK R / W", r3, Theme::EMERALD);
    snprintf(r4, sizeof(r4), "%.0f GB FREE", t.diskFreeGb);
    uint16_t diskCol = (t.diskFreeGb > 0 && t.diskFreeGb < 30.0f) ? Theme::CRIMSON
                       : ((t.diskFreeGb > 0 && t.diskFreeGb < 80.0f) ? Theme::AMBER : Theme::EMERALD);
    drawRow(166, "SYSTEM DISK", r4, diskCol);
    drawRow(184, "HOST", t.hostName, Theme::TEXT_MUTED);
}

void DisplayUI::drawMediaEqualizer(const DeckStore& store) {
    uint32_t ph = millis() / 180;
    _tft.fillRect(264, 38, 40, 22, Theme::BG_CARD);
    for (int i = 0; i < 6; i++) {
        int bh = store.telem.mediaPlaying ? (4 + ((ph + i * 3) % 5) * 3) : 4;
        _tft.fillRect(266 + i * 6, 58 - bh, 4, bh,
                      store.telem.mediaPlaying ? Theme::EMERALD : Theme::TEXT_DIM);
    }
}

void DisplayUI::drawMediaVolCard(const DeckStore& store) {
    const auto& t = store.telem;
    // Master Volume Card (x=8..206, y=148..200)
    _tft.fillSmoothRoundRect(8, 148, 198, 52, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(9, 149, 196, 50, 7, Theme::BG_CARD, Theme::BORDER_SUB);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(t.volMuted ? Theme::CRIMSON : Theme::TEXT_MUTED, Theme::BG_CARD);
    _tft.drawString(t.volMuted ? "VOLUME [MUTED]" : "MASTER VOLUME", 46, 155, 1);
    char vbuf[10];
    snprintf(vbuf, sizeof(vbuf), "%d%%", t.volumePct);
    _tft.setTextDatum(TR_DATUM);
    _tft.setTextColor(Theme::CYAN, Theme::BG_CARD);
    _tft.drawString(vbuf, 196, 154, 2);

    // Mute icon button
    _spr.fillRect(0, 0, 32, 36, Theme::BG_CARD);
    Icons::drawVectorIcon(_spr, t.volMuted ? "mute" : "vol_up", 16, 18, 20,
                          t.volMuted ? Theme::CRIMSON : Theme::CYAN, Theme::BG_CARD, Theme::CRIMSON);
    _spr.pushSprite(12, 156, 0, 0, 32, 36);

    // Larger tactile [-] stepper | Draggable Slider Bar | Larger tactile [+] stepper
    _tft.fillSmoothRoundRect(46, 169, 32, 26, 5, Theme::TRACK_DARK, Theme::BG_CARD);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::TRACK_DARK);
    _tft.drawString("-", 62, 182, 2);

    _tft.fillSmoothRoundRect(168, 169, 32, 26, 5, Theme::TRACK_DARK, Theme::BG_CARD);
    _tft.drawString("+", 184, 182, 2);

    _tft.fillRoundRect(86, 178, 74, 8, 4, Theme::TRACK_DARK);
    int vfw = (74 * t.volumePct) / 100;
    uint16_t vCol = t.volMuted ? Theme::CRIMSON : Theme::CYAN;
    if (vfw > 0) _tft.fillRoundRect(86, 178, max(6, vfw), 8, 4, vCol);
    int thumbX = constrain(86 + vfw, 91, 155);
    _tft.fillCircle(thumbX, 182, 6, Theme::TEXT_PRIMARY);
    _tft.fillCircle(thumbX, 182, 2, vCol);
}

void DisplayUI::drawMediaScreen(const DeckStore& store) {
    const auto& t = store.telem;
    _tft.fillRect(0, Theme::BODY_Y, Theme::W, Theme::BODY_H, Theme::BG_VOID);

    // Top Now Playing banner (8..312, y=30..85)
    _tft.fillSmoothRoundRect(8, 30, 304, 55, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(9, 31, 302, 53, 7, Theme::BG_CARD, Theme::BORDER_SUB);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(t.mediaPlaying ? Theme::EMERALD : Theme::CYAN, Theme::BG_CARD);
    _tft.drawString(t.mediaPlaying ? "NOW PLAYING // LIVE" : "MEDIA TRANSPORT", 18, 37, 1);

    char titleFit[42];
    fitTextToWidth(t.nowPlaying[0] ? t.nowPlaying : "Ready for Playback", 238, 2, titleFit, sizeof(titleFit));
    _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
    _tft.drawString(titleFit, 18, 54, 2);

    drawMediaEqualizer(store);

    // Transport buttons (PREV, PLAY/PAUSE, NEXT) at y=92..140
    const char* tIcons[3] = { "prev", t.mediaPlaying ? "pause" : "play", "next" };
    const char* tLbls[3]  = { "PREV", t.mediaPlaying ? "PAUSE" : "PLAY", "NEXT" };
    uint16_t tCols[3] = { Theme::BLUE, Theme::EMERALD, Theme::BLUE };
    for (int i = 0; i < 3; i++) {
        int x = 8 + i * 103;
        _spr.fillRect(0, 0, 98, 48, Theme::BG_VOID);
        _spr.fillSmoothRoundRect(0, 0, 98, 48, 8, Theme::BORDER_SUB, Theme::BG_VOID);
        _spr.fillSmoothRoundRect(1, 1, 96, 46, 7, Theme::BG_CARD, Theme::BORDER_SUB);
        Icons::drawVectorIcon(_spr, tIcons[i], 30, 24, 22, tCols[i], Theme::BG_CARD, Theme::CYAN);
        _spr.setTextDatum(ML_DATUM);
        _spr.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
        _spr.drawString(tLbls[i], 48, 24, 1);
        _spr.pushSprite(x, 92, 0, 0, 98, 48);
    }

    drawMediaVolCard(store);

    // Mic Mute giant tile (x=214..312, y=148..200)
    uint16_t mCol = t.micMuted ? Theme::CRIMSON : Theme::EMERALD;
    uint16_t mBg  = blendColor565(mCol, Theme::BG_CARD, 42);
    _spr.fillRect(0, 0, 98, 52, Theme::BG_VOID);
    _spr.fillSmoothRoundRect(0, 0, 98, 52, 8, mCol, Theme::BG_VOID);
    _spr.fillSmoothRoundRect(2, 2, 94, 48, 6, mBg, mCol);
    Icons::drawVectorIcon(_spr, t.micMuted ? "mic_off" : "mic", 25, 26, 22, mCol, mBg, Theme::CRIMSON);
    _spr.setTextDatum(MC_DATUM);
    _spr.setTextColor(Theme::TEXT_MUTED, mBg);
    _spr.drawString("MIC", 66, 15, 1);
    _spr.setTextColor(mCol, mBg);
    _spr.drawString(t.micMuted ? "MUTED" : "LIVE", 66, 33, 2);
    _spr.pushSprite(214, 148, 0, 0, 98, 52);
}

void DisplayUI::drawTimerRingCard() {
    uint32_t dispSec = 0;
    float prog = 0.0f;
    float elSecF = _tmrElapsedMs / 1000.0f;
    if (_tmrStopwatch) {
        dispSec = _tmrElapsedMs / 1000UL;
        prog = fmodf(elSecF, 60.0f) / 60.0f;
    } else {
        uint32_t elSec = _tmrElapsedMs / 1000UL;
        dispSec = (elSec >= _tmrTargetSec) ? 0 : (_tmrTargetSec - elSec);
        prog = (_tmrTargetSec > 0) ? constrain(elSecF / (float)_tmrTargetSec, 0.0f, 1.0f) : 0.0f;
    }
    _lastTimerSec = dispSec;

    // 132x148 sprite fits inside (8, 30, 140, 170) card with centered ring
    _spr.fillRect(0, 0, 132, 148, Theme::BG_CARD);
    uint16_t ringCol = _tmrRunning ? Theme::CYAN : Theme::AMBER;
    const int16_t cx = 66;
    const int16_t cy = 62;
    const int16_t r  = 50;
    const int16_t ir = 42;
    _spr.drawSmoothArc(cx, cy, r, ir, 0, 360, Theme::TRACK_DARK, Theme::BG_CARD, false);
    float visProg = (_tmrRunning && prog < 0.028f) ? 0.028f : prog;
    if (visProg >= 0.995f) {
        _spr.drawSmoothArc(cx, cy, r, ir, 0, 360, ringCol, Theme::BG_CARD, false);
    } else if (visProg > 0.005f) {
        uint16_t endA = (180 + (uint16_t)(358.0f * visProg)) % 360;
        if (endA == 180) endA = 190;
        _spr.drawSmoothArc(cx, cy, r, ir, 180, endA, ringCol, Theme::BG_CARD, true);
    }

    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
             (unsigned long)(dispSec / 60), (unsigned long)(dispSec % 60));
    _spr.setTextDatum(MC_DATUM);
    _spr.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
    _spr.drawString(tbuf, cx, cy - 4, 4);
    _spr.setTextColor(ringCol, Theme::BG_CARD);
    _spr.drawString(_tmrRunning ? "RUNNING" : (_tmrStopwatch ? "STOPWATCH" : "READY"), cx, cy + 20, 1);

    _spr.setTextColor(Theme::TEXT_MUTED, Theme::BG_CARD);
    _spr.drawString(_tmrRunning ? "TAP TO PAUSE" : "TAP TO START", cx, 136, 1);

    _spr.pushSprite(12, 41, 0, 0, 132, 148);
}

void DisplayUI::drawTimerScreen() {
    _tft.fillRect(0, Theme::BODY_Y, Theme::W, Theme::BODY_H, Theme::BG_VOID);

    // Left timer ring outer frame (8, 30, 140, 170)
    _tft.fillSmoothRoundRect(8, 30, 140, 170, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(9, 31, 138, 168, 7, Theme::BG_CARD, Theme::BORDER_SUB);
    drawTimerRingCard();

    // Right controls (Presets y=30..74, Start/Pause y=82..140, Reset y=148..200)
    const char* pNames[4] = { "5m", "15m", "25m", "STPW" };
    const uint32_t pVals[4] = { 300, 900, 1500, 0 };
    for (int i = 0; i < 4; i++) {
        int px = 156 + i * 40;
        bool sel = (i < 3) ? (!_tmrStopwatch && _tmrTargetSec == pVals[i]) : _tmrStopwatch;
        _tft.fillSmoothRoundRect(px, 30, 36, 44, 6, sel ? Theme::CYAN : Theme::BORDER_SUB, Theme::BG_VOID);
        _tft.fillSmoothRoundRect(px + 1, 31, 34, 42, 5, sel ? Theme::BG_CARD_ALT : Theme::BG_CARD,
                                 sel ? Theme::CYAN : Theme::BORDER_SUB);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(sel ? Theme::CYAN : Theme::TEXT_PRIMARY,
                          sel ? Theme::BG_CARD_ALT : Theme::BG_CARD);
        _tft.drawString(pNames[i], px + 18, 52, 1);
    }

    uint16_t spCol = _tmrRunning ? Theme::AMBER : Theme::EMERALD;
    _tft.fillSmoothRoundRect(156, 82, 156, 58, 8, spCol, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(158, 84, 152, 54, 6, Theme::BG_CARD_ALT, spCol);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(spCol, Theme::BG_CARD_ALT);
    _tft.drawString(_tmrRunning ? "PAUSE" : "START TIMER", 234, 111, 2);

    _tft.fillSmoothRoundRect(156, 148, 156, 52, 8, Theme::BORDER_HI, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(157, 149, 154, 50, 7, Theme::BG_CARD, Theme::BORDER_HI);
    _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
    _tft.drawString("RESET CLOCK", 234, 174, 2);
}

void DisplayUI::drawSysBacklightCard(const DeckStore& store) {
    const auto& s = store.settings;
    // Brightness slider + Wi-Fi/Link badge (8, 28, 304, 40)
    _tft.fillSmoothRoundRect(8, 28, 304, 40, 8, Theme::BORDER_SUB, Theme::BG_VOID);
    _tft.fillSmoothRoundRect(9, 29, 302, 38, 7, Theme::BG_CARD, Theme::BORDER_SUB);

    int blPct = map(s.brightness, 25, 255, 10, 100);
    char bbuf[24];
    snprintf(bbuf, sizeof(bbuf), "BACKLIGHT: %d%%", blPct);
    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
    _tft.drawString(bbuf, 18, 34, 1);

    char netLbl[28];
    if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        snprintf(netLbl, sizeof(netLbl), "WIFI: %s", ip.c_str());
        _tft.setTextColor(Theme::EMERALD, Theme::BG_CARD);
    } else {
        strlcpy(netLbl, store.telem.connected ? "LINK: USB SERIAL" : "LINK: STANDALONE", sizeof(netLbl));
        _tft.setTextColor(Theme::CYAN, Theme::BG_CARD);
    }
    _tft.setTextDatum(TR_DATUM);
    _tft.drawString(netLbl, 302, 34, 1);

    _tft.fillRoundRect(18, 52, 284, 7, 3, Theme::TRACK_DARK);
    int bw = map(s.brightness, 25, 255, 10, 284);
    _tft.fillRoundRect(18, 52, bw, 7, 3, Theme::CYAN);
    int thumbX = constrain(18 + bw, 24, 296);
    _tft.fillCircle(thumbX, 55, 6, Theme::TEXT_PRIMARY);
    _tft.fillCircle(thumbX, 55, 2, Theme::CYAN);
}

void DisplayUI::drawSysScreen(const DeckStore& store) {
    const auto& s = store.settings;
    _tft.fillRect(0, Theme::BODY_Y, Theme::W, Theme::BODY_H, Theme::BG_VOID);

    drawSysBacklightCard(store);

    // Row 0 & Row 1 (2x2 cards of 149x38 with sleek dark border + left color bar)
    const char* ledNames[4] = { "VITALS", "CYCLE", "CYAN", "OFF" };
    char lbl0[24], lbl1[24], lbl2[24], lbl3[24];
    snprintf(lbl0, sizeof(lbl0), "RGB: %s", ledNames[(int)s.ledMode % 4]);
    snprintf(lbl1, sizeof(lbl1), "AUDIO: %s", s.sfxEnabled ? "ON" : "OFF");
    if (s.sleepSec == 0) strlcpy(lbl2, "IDLE SLEEP: OFF", sizeof(lbl2));
    else snprintf(lbl2, sizeof(lbl2), "IDLE SLEEP: %dm", s.sleepSec / 60);
    snprintf(lbl3, sizeof(lbl3), "ORIENT: %s", (s.rotation == 3) ? "USB-L" : "USB-R");

    const char* topCards[4] = { lbl0, lbl1, lbl2, lbl3 };
    uint16_t topCols[4] = { Theme::VIOLET, s.sfxEnabled ? Theme::EMERALD : Theme::TEXT_MUTED,
                            Theme::BLUE, Theme::CYAN };

    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = 8 + col * 155;
        int y = 74 + row * 43;
        _tft.fillSmoothRoundRect(x, y, 149, 38, 7, Theme::BORDER_SUB, Theme::BG_VOID);
        _tft.fillSmoothRoundRect(x + 1, y + 1, 147, 36, 6, Theme::BG_CARD, Theme::BORDER_SUB);
        _tft.fillRoundRect(x + 6, y + 7, 3, 24, 1, topCols[i]);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
        _tft.drawString(topCards[i], x + 76, y + 19, 1);
    }

    // Row 2 (3 cards of 98x40): PANEL FIX | SLEEP NOW | CALIBRATE
    const char* panelMode = "STD";
    if (s.invertColors && s.swapRB) panelMode = "INV+BGR";
    else if (s.invertColors) panelMode = "INVERT";
    else if (s.swapRB) panelMode = "BGR";

    char lblPanel[20];
    snprintf(lblPanel, sizeof(lblPanel), "PANEL: %s", panelMode);
    const char* botCards[3] = { lblPanel, "SLEEP NOW", "CALIBRATE" };
    uint16_t botCols[3] = { Theme::AMBER, Theme::BLUE, Theme::CRIMSON };
    for (int i = 0; i < 3; i++) {
        int x = 8 + i * 103;
        int y = 160;
        _tft.fillSmoothRoundRect(x, y, 98, 40, 7, Theme::BORDER_SUB, Theme::BG_VOID);
        _tft.fillSmoothRoundRect(x + 1, y + 1, 96, 38, 6, Theme::BG_CARD, Theme::BORDER_SUB);
        _tft.fillRoundRect(x + 6, y + 8, 3, 24, 1, botCols[i]);
        _tft.setTextDatum(MC_DATUM);
        _tft.setTextColor(Theme::TEXT_PRIMARY, Theme::BG_CARD);
        _tft.drawString(botCards[i], x + 51, y + 20, 1);
    }
}
