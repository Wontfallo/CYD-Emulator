#include "TFT_eSPI.h"
#include "virtual_hw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Real TFT_eSPI bitmap and RLE font tables from PlatformIO library cache
#include "Fonts/glcdfont.c"
#include "Fonts/Font16.c"
#include "Fonts/Font32rle.c"

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

inline void decode565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    uint8_t r5 = static_cast<uint8_t>((c >> 11) & 0x1F);
    uint8_t g6 = static_cast<uint8_t>((c >> 5) & 0x3F);
    uint8_t b5 = static_cast<uint8_t>(c & 0x1F);
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

inline uint16_t encode565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
} // namespace

TFT_eSPI::TFT_eSPI(int16_t w, int16_t h)
    : _initW(w), _initH(h), _width(w), _height(h) {
    if (w > 0 && h > 0) {
        allocBuffer(w, h);
    }
}

void TFT_eSPI::allocBuffer(int32_t w, int32_t h) {
    _width = w;
    _height = h;
    if (w > 0 && h > 0) {
        _buf.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
    } else {
        _buf.clear();
    }
}

void TFT_eSPI::init() {
    if (_buf.empty() && _initW > 0 && _initH > 0) {
        allocBuffer(_initW, _initH);
    }
    setRotation(0);
    invertDisplay(false);
    fillScreen(TFT_BLACK);
}

void TFT_eSPI::setRotation(uint8_t r) {
    _rotation = r % 4;
    if (!_isSprite) {
        if (_rotation == 1 || _rotation == 3) {
            _width = _initH;
            _height = _initW;
        } else {
            _width = _initW;
            _height = _initH;
        }
        allocBuffer(_width, _height);

        uint8_t panel = vcyd::VirtualHw::instance().panelType();
        if (panel == 1) { // ST7789
            static constexpr uint8_t kSt[4] = {0x00, 0x60, 0xC0, 0xA0};
            _madctl = kSt[_rotation];
        } else { // ILI9341
            static constexpr uint8_t kIli[4] = {0x48, 0x28, 0x88, 0xE8};
            _madctl = kIli[_rotation];
        }
        vcyd::VirtualHw::instance().setDisplayMeta(_rotation, _madctl, _inverted);
    }
}

void TFT_eSPI::invertDisplay(bool i) {
    _inverted = i;
    if (!_isSprite) {
        vcyd::VirtualHw::instance().setDisplayMeta(_rotation, _madctl, _inverted);
    }
}

void TFT_eSPI::writecommand(uint8_t c) {
    _lastCmd = c;
    if (c == TFT_INVOFF) invertDisplay(false);
    else if (c == TFT_INVON) invertDisplay(true);
}

void TFT_eSPI::writedata(uint8_t d) {
    if (_lastCmd == TFT_MADCTL) {
        _madctl = d;
        if (!_isSprite) {
            vcyd::VirtualHw::instance().setDisplayMeta(_rotation, _madctl, _inverted);
        }
    }
}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || y < 0 || x >= _width || y >= _height || _buf.empty()) return;
    _buf[static_cast<size_t>(y) * static_cast<size_t>(_width) + static_cast<size_t>(x)] =
        static_cast<uint16_t>(color & 0xFFFF);
}

uint16_t TFT_eSPI::readPixel(int32_t x, int32_t y) const {
    if (x < 0 || y < 0 || x >= _width || y >= _height || _buf.empty()) return 0;
    return _buf[static_cast<size_t>(y) * static_cast<size_t>(_width) + static_cast<size_t>(x)];
}

uint16_t TFT_eSPI::blend565(uint16_t fg, uint16_t bg, uint8_t alpha) const {
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;
    uint8_t fr, fgC, fb, br, bgC, bb;
    decode565(fg, fr, fgC, fb);
    decode565(bg, br, bgC, bb);
    uint8_t inv = static_cast<uint8_t>(255 - alpha);
    uint8_t r = static_cast<uint8_t>((fr * alpha + br * inv + 127) / 255);
    uint8_t g = static_cast<uint8_t>((fgC * alpha + bgC * inv + 127) / 255);
    uint8_t b = static_cast<uint8_t>((fb * alpha + bb * inv + 127) / 255);
    return encode565(r, g, b);
}

void TFT_eSPI::blendPixel(int32_t x, int32_t y, uint16_t fg, uint32_t bg_hint, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= _width || y >= _height || alpha == 0) return;
    uint16_t bg = (bg_hint == 0x00FFFFFF) ? readPixel(x, y) : static_cast<uint16_t>(bg_hint & 0xFFFF);
    drawPixel(x, y, blend565(fg, bg, alpha));
}

void TFT_eSPI::fillScreen(uint32_t color) {
    uint16_t c = static_cast<uint16_t>(color & 0xFFFF);
    std::fill(_buf.begin(), _buf.end(), c);
}

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0 || _buf.empty()) return;
    int32_t x0 = max<int32_t>(0, x);
    int32_t y0 = max<int32_t>(0, y);
    int32_t x1 = min<int32_t>(_width, x + w);
    int32_t y1 = min<int32_t>(_height, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    uint16_t c = static_cast<uint16_t>(color & 0xFFFF);
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint16_t* row = &_buf[static_cast<size_t>(yy) * static_cast<size_t>(_width)];
        std::fill(row + x0, row + x1, c);
    }
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    fillRect(x, y, w, 1, color);
}

void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    fillRect(x, y, 1, h, color);
}

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    int32_t dx = std::abs(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -std::abs(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void TFT_eSPI::drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color) {
    if (wd <= 0.0f) return;
    float r = wd * 0.5f;
    int32_t minX = max<int32_t>(0, static_cast<int32_t>(std::floor(min(ax, bx) - r - 1.5f)));
    int32_t maxX = min<int32_t>(_width - 1, static_cast<int32_t>(std::ceil(max(ax, bx) + r + 1.5f)));
    int32_t minY = max<int32_t>(0, static_cast<int32_t>(std::floor(min(ay, by) - r - 1.5f)));
    int32_t maxY = min<int32_t>(_height - 1, static_cast<int32_t>(std::ceil(max(ay, by) + r + 1.5f)));

    float vx = bx - ax;
    float vy = by - ay;
    float len2 = vx * vx + vy * vy;
    uint16_t fg = static_cast<uint16_t>(fg_color & 0xFFFF);

    for (int32_t py = minY; py <= maxY; ++py) {
        for (int32_t px = minX; px <= maxX; ++px) {
            float wx = static_cast<float>(px) - ax;
            float wy = static_cast<float>(py) - ay;
            float t = (len2 > 1e-6f) ? ((wx * vx + wy * vy) / len2) : 0.0f;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
            float cx = wx - vx * t;
            float cy = wy - vy * t;
            float dist = std::sqrt(cx * cx + cy * cy);
            float cov = r + 0.5f - dist;
            if (cov >= 1.0f) {
                drawPixel(px, py, fg);
            } else if (cov > 0.0f) {
                blendPixel(px, py, fg, bg_color, static_cast<uint8_t>(cov * 255.0f));
            }
        }
    }
}

void TFT_eSPI::drawCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t cornername, uint32_t color) {
    int32_t f = 1 - r;
    int32_t ddF_x = 1;
    int32_t ddF_y = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (cornername & 0x4) { drawPixel(x0 + x, y0 + y, color); drawPixel(x0 + y, y0 + x, color); }
        if (cornername & 0x2) { drawPixel(x0 + x, y0 - y, color); drawPixel(x0 + y, y0 - x, color); }
        if (cornername & 0x8) { drawPixel(x0 - y, y0 + x, color); drawPixel(x0 - x, y0 + y, color); }
        if (cornername & 0x1) { drawPixel(x0 - y, y0 - x, color); drawPixel(x0 - x, y0 - y, color); }
    }
}

void TFT_eSPI::fillCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t corners, int32_t delta, uint32_t color) {
    int32_t f = 1 - r;
    int32_t ddF_x = 1;
    int32_t ddF_y = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (corners & 0x1) {
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, color);
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, color);
        }
        if (corners & 0x2) {
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, color);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, color);
        }
    }
}

void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int32_t maxR = min(w, h) / 2;
    if (r > maxR) r = maxR;
    if (r < 0) r = 0;
    drawFastHLine(x + r, y, w - 2 * r, color);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
    drawFastVLine(x, y + r, h - 2 * r, color);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
    if (r > 0) {
        drawCircleHelper(x + r, y + r, r, 1, color);
        drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
        drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
        drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
    }
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int32_t maxR = min(w, h) / 2;
    if (r > maxR) r = maxR;
    if (r < 0) r = 0;
    fillRect(x + r, y, w - 2 * r, h, color);
    if (r > 0) {
        fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
        fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
    }
}

void TFT_eSPI::fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint32_t bg_color) {
    if (w <= 0 || h <= 0) return;
    int32_t r = min(radius, min(w / 2, h / 2));
    if (r <= 1) {
        fillRect(x, y, w, h, color);
        return;
    }

    uint16_t fg = static_cast<uint16_t>(color & 0xFFFF);
    // Solid interior rectangles
    fillRect(x + r, y, w - 2 * r, h, fg);
    fillRect(x, y + r, r, h - 2 * r, fg);
    fillRect(x + w - r, y + r, r, h - 2 * r, fg);

    // Anti-aliased 4 corners
    int32_t cxL = x + r;
    int32_t cxR = x + w - r - 1;
    int32_t cyT = y + r;
    int32_t cyB = y + h - r - 1;
    float rf = static_cast<float>(r);

    for (int32_t dy = 0; dy <= r; ++dy) {
        for (int32_t dx = 0; dx <= r; ++dx) {
            float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            float cov = rf + 0.5f - d;
            if (cov <= 0.0f) continue;
            uint8_t alpha = (cov >= 1.0f) ? 255 : static_cast<uint8_t>(cov * 255.0f);
            int32_t pts[4][2] = {
                {cxL - dx, cyT - dy},
                {cxR + dx, cyT - dy},
                {cxL - dx, cyB + dy},
                {cxR + dx, cyB + dy},
            };
            for (auto& pt : pts) {
                if (alpha == 255) drawPixel(pt[0], pt[1], fg);
                else blendPixel(pt[0], pt[1], fg, bg_color, alpha);
            }
        }
    }
}

void TFT_eSPI::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
    if (r < 0) return;
    int32_t f = 1 - r;
    int32_t ddF_x = 1;
    int32_t ddF_y = -2 * r;
    int32_t x = 0;
    int32_t y = r;
    drawPixel(x0, y0 + r, color);
    drawPixel(x0, y0 - r, color);
    drawPixel(x0 + r, y0, color);
    drawPixel(x0 - r, y0, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);
        drawPixel(x0 - y, y0 + x, color);
        drawPixel(x0 + y, y0 - x, color);
        drawPixel(x0 - y, y0 - x, color);
    }
}

void TFT_eSPI::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
    if (r < 0) return;
    drawFastVLine(x0, y0 - r, 2 * r + 1, color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
}

void TFT_eSPI::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color) {
    if (r <= 0) {
        drawPixel(x, y, color);
        return;
    }
    uint16_t fg = static_cast<uint16_t>(color & 0xFFFF);
    float rf = static_cast<float>(r);
    int32_t minX = max<int32_t>(0, x - r - 1);
    int32_t maxX = min<int32_t>(_width - 1, x + r + 1);
    int32_t minY = max<int32_t>(0, y - r - 1);
    int32_t maxY = min<int32_t>(_height - 1, y + r + 1);
    for (int32_t py = minY; py <= maxY; ++py) {
        for (int32_t px = minX; px <= maxX; ++px) {
            float dx = static_cast<float>(px - x);
            float dy = static_cast<float>(py - y);
            float d = std::sqrt(dx * dx + dy * dy);
            float cov = rf + 0.5f - d;
            if (cov >= 1.0f) drawPixel(px, py, fg);
            else if (cov > 0.0f) blendPixel(px, py, fg, bg_color, static_cast<uint8_t>(cov * 255.0f));
        }
    }
}

void TFT_eSPI::drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color, bool roundEnds) {
    if (r < 0 || ir > r) return;
    uint16_t fg = static_cast<uint16_t>(fg_color & 0xFFFF);
    bool fullCircle = (startAngle == endAngle) || (startAngle == 0 && endAngle >= 360);
    uint32_t sa = startAngle % 360;
    uint32_t ea = endAngle % 360;

    float midR = (r + ir) * 0.5f;
    float halfThick = (r - ir) * 0.5f;
    float sRad = static_cast<float>(sa) * kDeg2Rad;
    float eRad = static_cast<float>(ea) * kDeg2Rad;
    float sx = -std::sin(sRad) * midR;
    float sy = +std::cos(sRad) * midR;
    float ex = -std::sin(eRad) * midR;
    float ey = +std::cos(eRad) * midR;

    int32_t minX = max<int32_t>(0, x - r - 2);
    int32_t maxX = min<int32_t>(_width - 1, x + r + 2);
    int32_t minY = max<int32_t>(0, y - r - 2);
    int32_t maxY = min<int32_t>(_height - 1, y + r + 2);

    for (int32_t py = minY; py <= maxY; ++py) {
        float dy = static_cast<float>(py - y);
        for (int32_t px = minX; px <= maxX; ++px) {
            float dx = static_cast<float>(px - x);
            float dist = std::sqrt(dx * dx + dy * dy);
            float cov = 0.0f;

            // Distance inside annulus band
            float bandDist = std::abs(dist - midR) - halfThick;
            if (bandDist < 0.7f) {
                if (fullCircle) {
                    cov = 0.7f - bandDist;
                } else {
                    float deg = std::atan2(-dx, dy) * (180.0f / kPi);
                    if (deg < 0.0f) deg += 360.0f;
                    bool inSector = (sa < ea)
                        ? (deg >= static_cast<float>(sa) && deg <= static_cast<float>(ea))
                        : (deg >= static_cast<float>(sa) || deg <= static_cast<float>(ea));
                    if (inSector) {
                        cov = 0.7f - bandDist;
                    }
                }
            }

            if (!fullCircle && roundEnds) {
                float ds = std::sqrt((dx - sx) * (dx - sx) + (dy - sy) * (dy - sy));
                float de = std::sqrt((dx - ex) * (dx - ex) + (dy - ey) * (dy - ey));
                float covS = halfThick + 0.5f - ds;
                float covE = halfThick + 0.5f - de;
                if (covS > cov) cov = covS;
                if (covE > cov) cov = covE;
            }

            if (cov >= 1.0f) {
                drawPixel(px, py, fg);
            } else if (cov > 0.0f) {
                blendPixel(px, py, fg, bg_color, static_cast<uint8_t>(cov * 255.0f));
            }
        }
    }
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    if (y0 == y2) {
        int32_t a = min(x0, min(x1, x2));
        int32_t b = max(x0, max(x1, x2));
        drawFastHLine(a, y0, b - a + 1, color);
        return;
    }

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;
    int32_t last = (y1 == y2) ? y1 : y1 - 1;
    int32_t y = y0;
    for (; y <= last; ++y) {
        int32_t a = x0 + sa / dy01;
        int32_t b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b) std::swap(a, b);
        drawFastHLine(a, y, b - a + 1, color);
    }
    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);
    for (; y <= y2; ++y) {
        int32_t a = x1 + sa / dy12;
        int32_t b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b) std::swap(a, b);
        drawFastHLine(a, y, b - a + 1, color);
    }
}

void TFT_eSPI::drawEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t color) {
    if (rx < 2) rx = 2;
    if (ry < 2) ry = 2;
    int32_t rx2 = rx * rx;
    int32_t ry2 = ry * ry;
    int32_t fx2 = 4 * rx2;
    int32_t fy2 = 4 * ry2;
    int32_t s = 2 * ry2 + rx2 * (1 - 2 * ry);
    for (int32_t x = 0, y = ry; ry2 * x <= rx2 * y; ++x) {
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        if (s >= 0) {
            s += fx2 * (1 - y);
            y--;
        }
        s += ry2 * ((4 * x) + 6);
    }
    s = 2 * rx2 + ry2 * (1 - 2 * rx);
    for (int32_t x = rx, y = 0; rx2 * y <= ry2 * x; ++y) {
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        if (s >= 0) {
            s += fy2 * (1 - x);
            x--;
        }
        s += rx2 * ((4 * y) + 6);
    }
}

int16_t TFT_eSPI::fontHeight(uint8_t font) const {
    int32_t scale = _textSize;
    if (font == 6 || font == 7) { font = 4; scale *= 2; }
    else if (font == 8) { font = 4; scale *= 3; }
    if (font == 1) return static_cast<int16_t>(8 * scale);
    if (font == 2) return static_cast<int16_t>(16 * scale);
    if (font == 4) return static_cast<int16_t>(26 * scale);
    return static_cast<int16_t>(8 * scale);
}

int16_t TFT_eSPI::textWidth(const char* string, uint8_t font) const {
    if (!string) return 0;
    int32_t scale = _textSize;
    if (font == 6 || font == 7) { font = 4; scale *= 2; }
    else if (font == 8) { font = 4; scale *= 3; }
    int32_t sum = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(string); *p; ++p) {
        unsigned char c = *p;
        if (font == 1) {
            sum += 6;
        } else if (font == 2) {
            if (c >= 32 && c < 128) sum += widtbl_f16[c - 32];
        } else if (font == 4) {
            if (c >= 32 && c < 128) sum += widtbl_f32[c - 32];
        } else {
            sum += 6;
        }
    }
    return static_cast<int16_t>(sum * scale);
}

int16_t TFT_eSPI::drawCharBitmap(uint16_t uniCode, int32_t x, int32_t y, uint8_t fontIndex) {
    int32_t scale = _textSize;
    if (fontIndex == 6 || fontIndex == 7) { fontIndex = 4; scale *= 2; }
    else if (fontIndex == 8) { fontIndex = 4; scale *= 3; }
    if (fontIndex == 1) {
        if (uniCode > 255) uniCode = '?';
        bool drawBg = (_textColor != _textBgColor);
        for (int8_t i = 0; i < 6; ++i) {
            uint8_t line = (i < 5) ? ::font[uniCode * 5 + i] : 0;
            for (int8_t j = 0; j < 8; ++j, line >>= 1) {
                if (line & 0x01) {
                    if (scale == 1) drawPixel(x + i, y + j, _textColor);
                    else fillRect(x + i * scale, y + j * scale, scale, scale, _textColor);
                } else if (drawBg) {
                    if (scale == 1) drawPixel(x + i, y + j, _textBgColor);
                    else fillRect(x + i * scale, y + j * scale, scale, scale, _textBgColor);
                }
            }
        }
        return static_cast<int16_t>(6 * scale);
    }

    if (uniCode < 32 || uniCode > 127) return 0;
    uint16_t idx = static_cast<uint16_t>(uniCode - 32);

    if (fontIndex == 2) {
        int32_t width = widtbl_f16[idx];
        const unsigned char* ptr = chrtbl_f16[idx];
        int32_t bytesPerRow = (width + 6) / 8;
        bool drawBg = (_textColor != _textBgColor);
        if (drawBg) {
            fillRect(x, y, width * scale, 16 * scale, _textBgColor);
        }
        for (int32_t row = 0; row < 16; ++row) {
            for (int32_t k = 0; k < bytesPerRow; ++k) {
                uint8_t bits = ptr[row * bytesPerRow + k];
                if (!bits) continue;
                for (int32_t b = 0; b < 8; ++b) {
                    if (bits & (0x80 >> b)) {
                        int32_t gx = k * 8 + b;
                        if (gx < width) {
                            if (scale == 1) drawPixel(x + gx, y + row, _textColor);
                            else fillRect(x + gx * scale, y + row * scale, scale, scale, _textColor);
                        }
                    }
                }
            }
        }
        return static_cast<int16_t>(width * scale);
    }

    if (fontIndex == 4) {
        int32_t width = widtbl_f32[idx];
        int32_t height = 26;
        const unsigned char* ptr = chrtbl_f32[idx];
        int32_t total = width * height;
        int32_t pc = 0;
        bool drawBg = (_textColor != _textBgColor);
        while (pc < total) {
            uint8_t run = *ptr++;
            bool fgRun = (run & 0x80) != 0;
            uint8_t count = static_cast<uint8_t>((run & 0x7F) + 1);
            uint16_t col = fgRun ? _textColor : _textBgColor;
            bool paint = fgRun || drawBg;
            for (uint8_t n = 0; n < count && pc < total; ++n, ++pc) {
                if (paint) {
                    int32_t px = pc % width;
                    int32_t py = pc / width;
                    if (scale == 1) drawPixel(x + px, y + py, col);
                    else fillRect(x + px * scale, y + py * scale, scale, scale, col);
                }
            }
        }
        return static_cast<int16_t>(width * scale);
    }

    return 0;
}

int16_t TFT_eSPI::drawString(const char* string, int32_t x, int32_t y, uint8_t fontIndex) {
    if (!string) return 0;
    int32_t cwidth = textWidth(string, fontIndex);
    int32_t cheight = fontHeight(fontIndex);
    int32_t poX = x;
    int32_t poY = y;

    switch (_textDatum) {
        case TC_DATUM: poX -= cwidth / 2; break;
        case TR_DATUM: poX -= cwidth; break;
        case ML_DATUM: poY -= cheight / 2; break;
        case MC_DATUM: poX -= cwidth / 2; poY -= cheight / 2; break;
        case MR_DATUM: poX -= cwidth; poY -= cheight / 2; break;
        case BL_DATUM: poY -= cheight; break;
        case BC_DATUM: poX -= cwidth / 2; poY -= cheight; break;
        case BR_DATUM: poX -= cwidth; poY -= cheight; break;
        default: break; // TL_DATUM
    }

    int32_t sumX = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(string); *p; ++p) {
        sumX += drawCharBitmap(*p, poX + sumX, poY, fontIndex);
    }
    return static_cast<int16_t>(sumX);
}

void* TFT_eSprite::createSprite(int16_t width, int16_t height, uint8_t frames) {
    (void)frames;
    allocBuffer(width, height);
    _created = (width > 0 && height > 0);
    return _buf.empty() ? nullptr : _buf.data();
}

void TFT_eSprite::deleteSprite() {
    allocBuffer(0, 0);
    _created = false;
}

void TFT_eSprite::pushSprite(int32_t x, int32_t y) {
    pushSprite(x, y, 0, 0, _width, _height);
}

void TFT_eSprite::pushSprite(int32_t tx, int32_t ty, int32_t sx, int32_t sy, int32_t sw, int32_t sh) {
    if (!_tft || !_created || _buf.empty()) return;
    // Strictly clip source region against the sprite's allocated buffer!
    // Matches real TFT_eSprite behavior: a 160x110 sprite clips anything beyond y=110.
    int32_t xStart = max<int32_t>(0, sx);
    int32_t yStart = max<int32_t>(0, sy);
    int32_t xEnd = min<int32_t>(_width, sx + sw);
    int32_t yEnd = min<int32_t>(_height, sy + sh);

    for (int32_t ry = yStart; ry < yEnd; ++ry) {
        int32_t dstY = ty + (ry - sy);
        for (int32_t rx = xStart; rx < xEnd; ++rx) {
            int32_t dstX = tx + (rx - sx);
            _tft->drawPixel(dstX, dstY, _buf[static_cast<size_t>(ry) * static_cast<size_t>(_width) + static_cast<size_t>(rx)]);
        }
    }
}

void TFT_eSPI::exportPanelRgba(uint8_t* out_rgba_320x240, uint8_t backlight_pwm) const {
    if (!out_rgba_320x240) return;
    uint8_t panel = vcyd::VirtualHw::instance().panelType();
    // Expected landscape MADCTL scan direction for physical panel:
    // ILI9341: rot1 => MV=0x20, BGR=0x08 | rot3 => MV|MX|MY=0xE0, BGR=0x08
    // ST7789 : rot1 => MX|MV=0x60, RGB=0x00 | rot3 => MY|MV=0xA0, RGB=0x00
    uint8_t expectedScan = 0x20;
    uint8_t expectedBgr = (panel == 0) ? 0x08 : 0x00;
    if (panel == 1) {
        expectedScan = (_rotation == 3) ? 0xA0 : 0x60;
    } else {
        expectedScan = (_rotation == 3) ? 0xE0 : 0x20;
    }

    uint8_t actualScan = static_cast<uint8_t>(_madctl & 0xE0);
    bool flipX = ((actualScan ^ expectedScan) & 0x40) != 0; // MX mismatch mirrors horizontal axis
    bool flipY = ((actualScan ^ expectedScan) & 0x80) != 0; // MY mismatch mirrors vertical axis
    bool swapRb = ((_madctl & 0x08) != expectedBgr);

    float lum = (backlight_pwm == 0)
        ? 0.0f
        : (backlight_pwm >= 200 ? 1.0f : (0.12f + 0.88f * (static_cast<float>(backlight_pwm) / 200.0f)));

    const int W = 320;
    const int H = 240;
    for (int y = 0; y < H; ++y) {
        int srcY = flipY ? (H - 1 - y) : y;
        for (int x = 0; x < W; ++x) {
            int srcX = flipX ? (W - 1 - x) : x;
            uint16_t px = readPixel(srcX, srcY);
            if (_inverted) px = static_cast<uint16_t>(~px);
            uint8_t r, g, b;
            decode565(px, r, g, b);
            if (swapRb) std::swap(r, b);
            size_t idx = (static_cast<size_t>(y) * W + static_cast<size_t>(x)) * 4;
            out_rgba_320x240[idx + 0] = static_cast<uint8_t>(r * lum);
            out_rgba_320x240[idx + 1] = static_cast<uint8_t>(g * lum);
            out_rgba_320x240[idx + 2] = static_cast<uint8_t>(b * lum);
            out_rgba_320x240[idx + 3] = 255;
        }
    }
}

