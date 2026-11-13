#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <cstring>

namespace Icons {

inline void drawThickLine(TFT_eSPI& g, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t w, uint16_t fg, uint16_t bg) {
    g.drawWideLine((float)x0, (float)y0, (float)x1, (float)y1, (float)w, fg, bg);
}

// Draws crisp vector iconography centered at (cx, cy), nominal bounding box `sz x sz` (typically 24..30).
inline void drawVectorIcon(TFT_eSPI& g, const char* name, int16_t cx, int16_t cy, int16_t sz,
                           uint16_t fg, uint16_t bg, uint16_t accent) {
    if (!name || !name[0]) name = "bolt";
    const int16_t r = sz / 2;

    if (strcmp(name, "mic") == 0 || strcmp(name, "mic_off") == 0) {
        bool off = (strcmp(name, "mic_off") == 0);
        uint16_t col = off ? accent : fg;
        // Capsule
        g.fillSmoothRoundRect(cx - r / 3, cy - r + 2, (r * 2) / 3, r + 2, r / 3, col, bg);
        // Cradle arc + stem + base
        g.drawSmoothArc(cx, cy + 1, r - 3, r - 5, 290, 70, col, bg, true);
        drawThickLine(g, cx, cy + r - 3, cx, cy + r, 2, col, bg);
        drawThickLine(g, cx - r / 2, cy + r, cx + r / 2, cy + r, 2, col, bg);
        if (off) {
            drawThickLine(g, cx - r + 2, cy + r - 2, cx + r - 2, cy - r + 2, 3, bg, bg);
            drawThickLine(g, cx - r + 2, cy + r - 2, cx + r - 2, cy - r + 2, 2, accent, bg);
        }
        return;
    }

    if (strcmp(name, "cam") == 0 || strcmp(name, "obs") == 0) {
        g.fillSmoothRoundRect(cx - r + 1, cy - r / 2 - 1, sz - 8, sz / 2 + 4, 3, fg, bg);
        g.fillTriangle(cx + r - 7, cy, cx + r, cy - r / 2 + 1, cx + r, cy + r / 2 - 1, fg);
        g.fillSmoothCircle(cx - 3, cy + 1, 3, accent, fg);
        return;
    }

    if (strcmp(name, "rec") == 0) {
        g.drawSmoothArc(cx, cy, r - 1, r - 3, 0, 360, fg, bg, false);
        g.fillSmoothCircle(cx, cy, r / 2 + 1, accent, bg);
        return;
    }

    if (strcmp(name, "play") == 0) {
        g.fillTriangle(cx - r / 2 + 1, cy - r + 3, cx - r / 2 + 1, cy + r - 3, cx + r - 2, cy, fg);
        return;
    }

    if (strcmp(name, "pause") == 0) {
        int16_t bw = max<int16_t>(3, r / 3);
        g.fillSmoothRoundRect(cx - r / 2 - 1, cy - r + 3, bw, sz - 6, 1, fg, bg);
        g.fillSmoothRoundRect(cx + r / 2 - bw + 1, cy - r + 3, bw, sz - 6, 1, fg, bg);
        return;
    }

    int16_t th = max<int16_t>(3, r - 4);
    int16_t barW = (sz >= 18) ? 3 : 2;
    if (strcmp(name, "next") == 0) {
        g.fillTriangle(cx - r + 1, cy - th, cx - r + 1, cy + th, cx - 2, cy, fg);
        g.fillTriangle(cx - 1, cy - th, cx - 1, cy + th, cx + r - barW - 2, cy, fg);
        g.fillRect(cx + r - barW, cy - th, barW, th * 2 + 1, fg);
        return;
    }

    if (strcmp(name, "prev") == 0) {
        g.fillRect(cx - r, cy - th, barW, th * 2 + 1, fg);
        g.fillTriangle(cx - r + barW + 1, cy, cx, cy - th, cx, cy + th, fg);
        g.fillTriangle(cx + 1, cy, cx + r - 2, cy - th, cx + r - 2, cy + th, fg);
        return;
    }

    if (strcmp(name, "vol_up") == 0 || strcmp(name, "vol_dn") == 0 || strcmp(name, "mute") == 0) {
        // Speaker cone
        g.fillRect(cx - r + 1, cy - 4, 5, 9, fg);
        g.fillTriangle(cx - r + 5, cy - 4, cx - r + 5, cy + 4, cx + 1, cy - r + 3, fg);
        g.fillTriangle(cx - r + 5, cy + 4, cx + 1, cy - r + 3, cx + 1, cy + r - 3, fg);
        if (strcmp(name, "vol_up") == 0) {
            g.drawSmoothArc(cx + 1, cy, r - 4, r - 6, 225, 315, accent, bg, true);
            g.drawSmoothArc(cx + 1, cy, r, r - 2, 220, 320, fg, bg, true);
        } else if (strcmp(name, "vol_dn") == 0) {
            g.drawSmoothArc(cx + 1, cy, r - 3, r - 5, 225, 315, fg, bg, true);
        } else {
            drawThickLine(g, cx + 4, cy - 4, cx + r - 1, cy + 4, 2, accent, bg);
            drawThickLine(g, cx + r - 1, cy - 4, cx + 4, cy + 4, 2, accent, bg);
        }
        return;
    }

    if (strcmp(name, "terminal") == 0) {
        g.fillSmoothRoundRect(cx - r, cy - r + 2, sz, sz - 4, 4, fg, bg);
        g.fillSmoothRoundRect(cx - r + 2, cy - r + 4, sz - 4, sz - 8, 2, bg, fg);
        drawThickLine(g, cx - r + 5, cy - 3, cx - 2, cy, 2, accent, bg);
        drawThickLine(g, cx - 2, cy, cx - r + 5, cy + 3, 2, accent, bg);
        drawThickLine(g, cx + 1, cy + 4, cx + r - 5, cy + 4, 2, fg, bg);
        return;
    }

    if (strcmp(name, "code") == 0) {
        drawThickLine(g, cx - r + 5, cy - 4, cx - r + 1, cy, 2, fg, bg);
        drawThickLine(g, cx - r + 1, cy, cx - r + 5, cy + 4, 2, fg, bg);
        drawThickLine(g, cx + r - 5, cy - 4, cx + r - 1, cy, 2, fg, bg);
        drawThickLine(g, cx + r - 1, cy, cx + r - 5, cy + 4, 2, fg, bg);
        drawThickLine(g, cx + 2, cy - r + 3, cx - 2, cy + r - 3, 2, accent, bg);
        return;
    }

    if (strcmp(name, "git") == 0) {
        drawThickLine(g, cx - 4, cy - r + 4, cx - 4, cy + r - 4, 2, fg, bg);
        drawThickLine(g, cx - 4, cy + 3, cx + 5, cy - 3, 2, accent, bg);
        g.fillSmoothCircle(cx - 4, cy - r + 4, 3, fg, bg);
        g.fillSmoothCircle(cx - 4, cy + r - 4, 3, fg, bg);
        g.fillSmoothCircle(cx + 5, cy - 3, 3, accent, bg);
        return;
    }

    if (strcmp(name, "browser") == 0) {
        g.drawSmoothArc(cx, cy, r - 1, r - 3, 0, 360, fg, bg, false);
        g.drawFastHLine(cx - r + 3, cy, sz - 6, accent);
        g.drawFastVLine(cx, cy - r + 3, sz - 6, accent);
        g.drawEllipse(cx, cy, r / 2, r - 2, fg);
        return;
    }

    if (strcmp(name, "discord") == 0) {
        g.fillSmoothRoundRect(cx - r + 1, cy - r + 4, sz - 2, sz - 8, 6, fg, bg);
        g.fillSmoothCircle(cx - 4, cy, 2, bg, fg);
        g.fillSmoothCircle(cx + 4, cy, 2, bg, fg);
        return;
    }

    if (strcmp(name, "shot") == 0) {
        // Viewfinder corners + shutter dot
        int16_t d = r - 1;
        g.drawFastHLine(cx - d, cy - d, 6, fg); g.drawFastVLine(cx - d, cy - d, 6, fg);
        g.drawFastHLine(cx + d - 5, cy - d, 6, fg); g.drawFastVLine(cx + d, cy - d, 6, fg);
        g.drawFastHLine(cx - d, cy + d, 6, fg); g.drawFastVLine(cx - d, cy + d - 5, 6, fg);
        g.drawFastHLine(cx + d - 5, cy + d, 6, fg); g.drawFastVLine(cx + d, cy + d - 5, 6, fg);
        g.drawCircle(cx, cy, r / 2, fg);
        g.fillSmoothCircle(cx, cy, 3, accent, bg);
        return;
    }

    if (strcmp(name, "lock") == 0) {
        g.drawSmoothArc(cx, cy - 3, r / 2, r / 2 - 2, 90, 270, fg, bg, true);
        g.fillSmoothRoundRect(cx - r / 2 - 2, cy - 2, r + 4, r, 3, fg, bg);
        g.fillSmoothCircle(cx, cy + 3, 2, bg, fg);
        return;
    }

    if (strcmp(name, "rocket") == 0) {
        drawThickLine(g, cx - 5, cy + 5, cx + 6, cy - 6, 4, fg, bg);
        g.fillTriangle(cx + 2, cy - r + 2, cx + r - 1, cy - r + 2, cx + r - 1, cy - 2, accent);
        g.fillSmoothCircle(cx - 6, cy + 6, 3, accent, bg);
        return;
    }

    if (strcmp(name, "cpu") == 0) {
        int16_t s = r - 4;
        g.fillSmoothRoundRect(cx - s, cy - s, s * 2, s * 2, 3, fg, bg);
        g.fillRect(cx - s + 3, cy - s + 3, s * 2 - 6, s * 2 - 6, accent);
        for (int i = -1; i <= 1; i++) {
            int16_t off = i * 4;
            g.drawFastVLine(cx + off, cy - r, 3, fg);
            g.drawFastVLine(cx + off, cy + r - 3, 3, fg);
            g.drawFastHLine(cx - r, cy + off, 3, fg);
            g.drawFastHLine(cx + r - 3, cy + off, 3, fg);
        }
        return;
    }

    if (strcmp(name, "gpu") == 0) {
        g.fillSmoothRoundRect(cx - r, cy - r / 2 - 2, sz, r + 4, 3, fg, bg);
        g.fillSmoothCircle(cx - 4, cy, 4, bg, fg);
        g.fillSmoothCircle(cx + 5, cy, 4, bg, fg);
        g.fillSmoothCircle(cx - 4, cy, 2, accent, bg);
        g.fillSmoothCircle(cx + 5, cy, 2, accent, bg);
        return;
    }

    if (strcmp(name, "ram") == 0) {
        g.fillSmoothRoundRect(cx - r, cy - 5, sz, 11, 2, fg, bg);
        for (int i = -2; i <= 2; i++) {
            g.fillRect(cx + i * 4 - 1, cy - 3, 3, 6, accent);
            g.drawFastVLine(cx + i * 4, cy + 6, 3, fg);
        }
        return;
    }

    if (strcmp(name, "folder") == 0) {
        g.fillSmoothRoundRect(cx - r, cy - r + 2, r, 6, 2, accent, bg);
        g.fillSmoothRoundRect(cx - r, cy - r + 6, sz, sz - 8, 3, fg, bg);
        g.fillSmoothRoundRect(cx - r + 2, cy - r + 8, sz - 4, sz - 12, 2, bg, fg);
        g.drawFastHLine(cx - r + 4, cy - r + 9, sz - 8, accent);
        return;
    }

    if (strcmp(name, "back") == 0) {
        drawThickLine(g, cx - r + 2, cy, cx + r - 2, cy, 2, fg, bg);
        drawThickLine(g, cx - r + 2, cy, cx - 1, cy - 6, 2, fg, bg);
        drawThickLine(g, cx - r + 2, cy, cx - 1, cy + 6, 2, fg, bg);
        return;
    }

    if (strcmp(name, "webhook") == 0) {
        g.fillSmoothCircle(cx, cy - 5, 3, fg, bg);
        g.fillSmoothCircle(cx - 5, cy + 5, 3, accent, bg);
        g.fillSmoothCircle(cx + 5, cy + 5, 3, fg, bg);
        drawThickLine(g, cx, cy - 5, cx - 5, cy + 5, 2, fg, bg);
        drawThickLine(g, cx, cy - 5, cx + 5, cy + 5, 2, accent, bg);
        return;
    }

    if (strcmp(name, "flame") == 0) {
        g.fillSmoothCircle(cx, cy + 3, r / 2 + 2, accent, bg);
        g.fillTriangle(cx - r / 2, cy + 1, cx + r / 2, cy + 1, cx, cy - r + 1, accent);
        g.fillSmoothCircle(cx, cy + 4, r / 3, fg, accent);
        return;
    }

    if (strcmp(name, "sparkles") == 0) {
        drawThickLine(g, cx - 2, cy - r + 2, cx - 2, cy + r - 2, 2, fg, bg);
        drawThickLine(g, cx - r + 2, cy, cx + r - 6, cy, 2, fg, bg);
        g.fillSmoothCircle(cx + 6, cy - 6, 3, accent, bg);
        return;
    }

    if (strcmp(name, "timer") == 0) {
        g.drawSmoothArc(cx, cy + 1, r - 2, r - 4, 0, 360, fg, bg, false);
        drawThickLine(g, cx - 3, cy - r + 1, cx + 3, cy - r + 1, 2, accent, bg);
        drawThickLine(g, cx, cy + 1, cx, cy - 5, 2, accent, bg);
        drawThickLine(g, cx, cy + 1, cx + 5, cy + 3, 2, fg, bg);
        return;
    }

    if (strcmp(name, "settings") == 0) {
        g.drawSmoothArc(cx, cy, r - 3, r - 6, 0, 360, fg, bg, false);
        g.fillSmoothCircle(cx, cy, 3, accent, bg);
        for (int a = 0; a < 4; a++) {
            float rad = a * 0.7854f;
            int16_t dx = (int16_t)(cosf(rad) * (r - 1));
            int16_t dy = (int16_t)(sinf(rad) * (r - 1));
            drawThickLine(g, cx - dx, cy - dy, cx + dx, cy + dy, 2, fg, bg);
        }
        g.fillSmoothCircle(cx, cy, 4, bg, fg);
        g.fillSmoothCircle(cx, cy, 2, accent, bg);
        return;
    }

    if (strcmp(name, "window") == 0) {
        g.fillSmoothRoundRect(cx - r, cy - r + 2, sz, sz - 4, 3, fg, bg);
        g.fillRect(cx - r + 2, cy - r + 7, sz - 4, sz - 11, bg);
        g.drawFastVLine(cx, cy - r + 7, sz - 11, accent);
        return;
    }

    if (strcmp(name, "music") == 0) {
        g.fillSmoothCircle(cx - 5, cy + 5, 4, fg, bg);
        g.fillSmoothCircle(cx + 5, cy + 3, 4, fg, bg);
        drawThickLine(g, cx - 2, cy + 5, cx - 2, cy - 7, 2, fg, bg);
        drawThickLine(g, cx + 8, cy + 3, cx + 8, cy - 9, 2, fg, bg);
        drawThickLine(g, cx - 2, cy - 7, cx + 8, cy - 9, 3, accent, bg);
        return;
    }

    if (strcmp(name, "clipboard") == 0) {
        g.fillSmoothRoundRect(cx - r + 3, cy - r + 4, sz - 6, sz - 6, 3, fg, bg);
        g.fillSmoothRoundRect(cx - 4, cy - r + 1, 8, 5, 2, accent, bg);
        g.drawFastHLine(cx - 4, cy, 8, bg);
        g.drawFastHLine(cx - 4, cy + 4, 6, bg);
        return;
    }

    // Default: "bolt" lightning glyph
    g.fillTriangle(cx + 2, cy - r + 1, cx - r / 2 - 1, cy + 2, cx + 1, cy + 2, fg);
    g.fillTriangle(cx - 1, cy - 1, cx + r / 2 + 1, cy - 1, cx - 3, cy + r - 1, accent);
}

} // namespace Icons

