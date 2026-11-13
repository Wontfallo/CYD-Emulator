#pragma once

#include "Arduino.h"
#include <cstdint>
#include <vector>

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define CL_DATUM 3
#define MC_DATUM 4
#define CC_DATUM 4
#define MR_DATUM 5
#define CR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

#define TFT_BLACK   0x0000
#define TFT_WHITE   0xFFFF
#define TFT_RED     0xF800
#define TFT_GREEN   0x07E0
#define TFT_BLUE    0x001F
#define TFT_CYAN    0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW  0xFFE0

#define TFT_MADCTL  0x36
#define TFT_INVOFF  0x20
#define TFT_INVON   0x21

class TFT_eSPI {
public:
    explicit TFT_eSPI(int16_t w = 240, int16_t h = 320);
    virtual ~TFT_eSPI() = default;

    void init();
    void begin() { init(); }
    void setRotation(uint8_t r);
    uint8_t getRotation() const { return _rotation; }
    void invertDisplay(bool i);
    void writecommand(uint8_t c);
    void writedata(uint8_t d);
    void setSwapBytes(bool swap) { _swapBytes = swap; }

    int32_t width() const { return _width; }
    int32_t height() const { return _height; }

    virtual void drawPixel(int32_t x, int32_t y, uint32_t color);
    uint16_t readPixel(int32_t x, int32_t y) const;

    void fillScreen(uint32_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
    void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF);

    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
    void fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint32_t bg_color = 0x00FFFFFF);

    void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color);
    void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color);
    void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color = 0x00FFFFFF);
    void drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds = false);

    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
    void drawEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t color);

    void setTextDatum(uint8_t datum) { _textDatum = datum; }
    void setTextColor(uint16_t fg) { _textColor = fg; _textBgColor = fg; }
    void setTextColor(uint16_t fg, uint16_t bg) { _textColor = fg; _textBgColor = bg; }
    void setTextSize(uint8_t s) { _textSize = s ? s : 1; }
    void setTextFont(uint8_t f) { _textFont = f; }

    int16_t textWidth(const char* string, uint8_t font = 1) const;
    int16_t textWidth(const String& string, uint8_t font = 1) const { return textWidth(string.c_str(), font); }
    int16_t fontHeight(uint8_t font) const;

    int16_t drawString(const char* string, int32_t x, int32_t y, uint8_t font);
    int16_t drawString(const String& string, int32_t x, int32_t y, uint8_t font) {
        return drawString(string.c_str(), x, y, font);
    }

    // Emulator inspection hook: reads panel output after applying 0x36 MADCTL & inversion & backlight
    void exportPanelRgba(uint8_t* out_rgba_320x240, uint8_t backlight_pwm) const;

protected:
    void allocBuffer(int32_t w, int32_t h);
    uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) const;
    void blendPixel(int32_t x, int32_t y, uint16_t fg, uint32_t bg_hint, uint8_t alpha);
    void fillCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t corners, int32_t delta, uint32_t color);
    void drawCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, uint32_t color);
    int16_t drawCharBitmap(uint16_t uniCode, int32_t x, int32_t y, uint8_t font);

    int32_t _initW = 240;
    int32_t _initH = 320;
    int32_t _width = 240;
    int32_t _height = 320;
    uint8_t _rotation = 0;
    uint8_t _madctl = 0x08;
    uint8_t _lastCmd = 0;
    bool _inverted = false;
    bool _swapBytes = false;
    bool _isSprite = false;

    uint8_t _textDatum = TL_DATUM;
    uint16_t _textColor = 0xFFFF;
    uint16_t _textBgColor = 0xFFFF;
    uint8_t _textSize = 1;
    uint8_t _textFont = 1;

    std::vector<uint16_t> _buf;
};

class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI* tft) : TFT_eSPI(0, 0), _tft(tft) {
        _isSprite = true;
    }

    void setColorDepth(int8_t b) { _depth = b; }
    void* createSprite(int16_t width, int16_t height, uint8_t frames = 1);
    void deleteSprite();
    bool created() const { return _created; }

    void pushSprite(int32_t x, int32_t y);
    void pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh);

private:
    TFT_eSPI* _tft = nullptr;
    int8_t _depth = 16;
    bool _created = false;
};

