#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "protocol.h"

class DeckStore {
public:
    void begin();

    DeckPage pages[MAX_DECK_PAGES];
    uint8_t pageCount = 4;
    DeviceSettings settings{};
    TelemetryState telem{};

    void loadDefaults();
    bool saveDeckToFS();
    bool loadDeckFromFS();
    void saveSettings();
    void loadSettings();

    bool applyDeckJson(JsonArrayConst pagesArr);
    bool applyTileStateJson(uint8_t pageIdx, uint8_t tileIdx, bool isOn);
    bool applyTelemetryJson(JsonObjectConst doc);
    void pushTelemHistory();

private:
    static void setTile(DeckTile& t, const char* lbl, const char* sub, const char* ico,
                        uint16_t col, bool tog, bool on, int8_t jump);
};

