#pragma once
#include <stdint.h>

// HYDRO-DECK Obsidian Cyber-Glass Palette (RGB565)
// Designed specifically for crisp high-contrast legibility on 2.8" 320x240 TFT

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

namespace Theme {
    // Surfaces
    constexpr uint16_t BG_VOID      = rgb565(8, 10, 16);      // #080A10 deep obsidian canvas
    constexpr uint16_t BG_SURFACE   = rgb565(15, 20, 31);     // #0F141F card / dock fill
    constexpr uint16_t BG_CARD      = rgb565(20, 27, 42);     // #141B2A elevated tile
    constexpr uint16_t BG_CARD_ALT  = rgb565(25, 34, 52);     // #192234 hover / active surface
    constexpr uint16_t BG_DOCK      = rgb565(12, 16, 25);     // #0C1019 top/bottom chrome

    // Hairlines & Borders
    constexpr uint16_t BORDER_SUB   = rgb565(34, 46, 68);     // #222E44 subtle stroke
    constexpr uint16_t BORDER_HI    = rgb565(58, 78, 112);    // #3A4E70 bright edge
    constexpr uint16_t TRACK_DARK   = rgb565(40, 52, 78);     // #28344E crisp gauge track

    // Text
    constexpr uint16_t TEXT_PRIMARY = rgb565(242, 246, 255);  // #F2F6FF crisp off-white
    constexpr uint16_t TEXT_MUTED   = rgb565(138, 154, 184);  // #8A9AB8 cool slate
    constexpr uint16_t TEXT_DIM     = rgb565(78, 92, 118);    // #4E5C76 subtle label

    // Neon Accents
    constexpr uint16_t CYAN         = rgb565(0, 240, 218);    // #00F0DA signature hydro cyan
    constexpr uint16_t BLUE         = rgb565(56, 152, 255);   // #3898FF electric cobalt
    constexpr uint16_t EMERALD      = rgb565(34, 232, 138);   // #22E88A GPU / healthy lime
    constexpr uint16_t VIOLET       = rgb565(168, 102, 255);  // #A866FF RAM / synth violet
    constexpr uint16_t AMBER        = rgb565(255, 182, 46);   // #FFB62E warm alert
    constexpr uint16_t CRIMSON      = rgb565(255, 58, 98);    // #FF3A62 danger / REC / mute
    constexpr uint16_t GOLD         = rgb565(255, 212, 82);   // #FFD452 highlight

    // Layout Constants (320x240 Landscape — zero gap between bands)
    constexpr int W = 320;
    constexpr int H = 240;
    constexpr int TOP_BAR_H = 26;
    constexpr int BOT_BAR_H = 34;
    constexpr int BODY_Y = TOP_BAR_H;
    constexpr int BODY_H = H - TOP_BAR_H - BOT_BAR_H; // 180px seamless workspace (26..206)
}
