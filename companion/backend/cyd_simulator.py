from __future__ import annotations

import io
import math
from typing import Any

from PIL import Image, ImageDraw, ImageFont

# Mirrors firmware/include/ui_theme.h palette identically (RGB tuples)
BG_VOID      = (8, 10, 16)
BG_SURFACE   = (15, 20, 31)
BG_CARD      = (20, 27, 42)
BG_CARD_ALT  = (25, 34, 52)
BG_DOCK      = (12, 16, 25)
BORDER_SUB   = (34, 46, 68)
BORDER_HI    = (58, 78, 112)
TRACK_DARK   = (40, 52, 78)
TEXT_PRIMARY = (242, 246, 255)
TEXT_MUTED   = (138, 154, 184)
TEXT_DIM     = (78, 92, 118)
CYAN         = (0, 240, 218)
BLUE         = (56, 152, 255)
EMERALD      = (34, 232, 138)
VIOLET       = (168, 102, 255)
AMBER        = (255, 182, 46)
CRIMSON      = (255, 58, 98)


def hex_rgb(h: str, fallback: tuple[int, int, int] = CYAN) -> tuple[int, int, int]:
    try:
        s = h.lstrip("#")
        if len(s) != 6:
            return fallback
        return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))
    except Exception:
        return fallback


def load_color(pct: float, base: tuple[int, int, int]) -> tuple[int, int, int]:
    if pct >= 90:
        return CRIMSON
    if pct >= 74:
        return AMBER
    return base


def _font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"]
        if bold
        else ["C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"]
    )
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except Exception:
            continue
    return ImageFont.load_default()


F_XS  = _font(18, False)
F_SM  = _font(21, True)
F_MD  = _font(26, True)
F_LG  = _font(38, True)
F_XL  = _font(48, True)

# Render at 2x (640x480) then LANCZOS downsample to crisp 320x240 so arcs & fonts look identical to anti-aliased TFT_eSPI
SCALE = 2


def _draw_vector_icon(draw: ImageDraw.ImageDraw, name: str, cx: int, cy: int, sz: int,
                      fg: tuple[int, int, int], bg: tuple[int, int, int], accent: tuple[int, int, int]) -> None:
    r = sz // 2
    name = (name or "bolt").lower()
    if name in ("mic", "mic_off"):
        off = name == "mic_off"
        col = accent if off else fg
        draw.rounded_rectangle([cx - r // 3, cy - r + 2, cx + r // 3, cy + 4], radius=r // 3, fill=col)
        draw.arc([cx - r + 4, cy - r + 8, cx + r - 4, cy + r - 4], start=0, end=180, fill=col, width=4)
        draw.line([cx, cy + r - 5, cx, cy + r], fill=col, width=4)
        draw.line([cx - r // 2, cy + r, cx + r // 2, cy + r], fill=col, width=4)
        if off:
            draw.line([cx - r + 3, cy + r - 3, cx + r - 3, cy - r + 3], fill=accent, width=4)
    elif name in ("cam", "obs"):
        draw.rounded_rectangle([cx - r + 2, cy - r // 2, cx + r - 12, cy + r // 2 + 2], radius=5, fill=fg)
        draw.polygon([(cx + r - 12, cy), (cx + r, cy - r // 2), (cx + r, cy + r // 2)], fill=fg)
        draw.ellipse([cx - 10, cy - 5, cx, cy + 5], fill=accent)
    elif name == "rec":
        draw.ellipse([cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2], outline=fg, width=4)
        draw.ellipse([cx - r // 2, cy - r // 2, cx + r // 2, cy + r // 2], fill=accent)
    elif name == "play":
        draw.polygon([(cx - r // 2 + 2, cy - r + 4), (cx - r // 2 + 2, cy + r - 4), (cx + r - 3, cy)], fill=fg)
    elif name == "pause":
        draw.rounded_rectangle([cx - r // 2 - 2, cy - r + 5, cx - 4, cy + r - 5], radius=2, fill=fg)
        draw.rounded_rectangle([cx + 4, cy - r + 5, cx + r // 2 + 2, cy + r - 5], radius=2, fill=fg)
    elif name == "next":
        draw.polygon([(cx - r + 3, cy - r + 6), (cx - r + 3, cy + r - 6), (cx + 3, cy)], fill=fg)
        draw.polygon([(cx - 1, cy - r + 6), (cx - 1, cy + r - 6), (cx + r - 5, cy)], fill=fg)
        draw.rectangle([cx + r - 5, cy - r + 6, cx + r, cy + r - 6], fill=fg)
    elif name == "prev":
        draw.rectangle([cx - r, cy - r + 6, cx - r + 5, cy + r - 6], fill=fg)
        draw.polygon([(cx + 1, cy - r + 6), (cx + 1, cy + r - 6), (cx - r + 5, cy)], fill=fg)
        draw.polygon([(cx + r - 3, cy - r + 6), (cx + r - 3, cy + r - 6), (cx - 3, cy)], fill=fg)
    elif name in ("vol_up", "vol_dn", "mute"):
        draw.polygon([
            (cx - r + 2, cy - 8), (cx - r + 10, cy - 8), (cx + 2, cy - r + 5),
            (cx + 2, cy + r - 5), (cx - r + 10, cy + 8), (cx - r + 2, cy + 8),
        ], fill=fg)
        if name == "vol_up":
            draw.arc([cx - 6, cy - r + 8, cx + r - 6, cy + r - 8], start=-50, end=50, fill=accent, width=4)
            draw.arc([cx - 2, cy - r, cx + r, cy + r], start=-55, end=55, fill=fg, width=4)
        elif name == "vol_dn":
            draw.arc([cx - 4, cy - r + 8, cx + r - 4, cy + r - 8], start=-45, end=45, fill=fg, width=4)
        else:
            draw.line([cx + 7, cy - 8, cx + r - 1, cy + 8], fill=accent, width=4)
            draw.line([cx + r - 1, cy - 8, cx + 7, cy + 8], fill=accent, width=4)
    elif name == "terminal":
        draw.rounded_rectangle([cx - r, cy - r + 4, cx + r, cy + r - 4], radius=7, outline=fg, width=4, fill=bg)
        draw.line([cx - r + 9, cy - 6, cx - 3, cy, cx - r + 9, cy + 6], fill=accent, width=4)
        draw.line([cx + 2, cy + 7, cx + r - 8, cy + 7], fill=fg, width=4)
    elif name == "code":
        draw.line([cx - r + 9, cy - 8, cx - r + 2, cy, cx - r + 9, cy + 8], fill=fg, width=4)
        draw.line([cx + r - 9, cy - 8, cx + r - 2, cy, cx + r - 9, cy + 8], fill=fg, width=4)
        draw.line([cx + 4, cy - r + 5, cx - 4, cy + r - 5], fill=accent, width=4)
    elif name == "git":
        draw.line([cx - 8, cy - r + 7, cx - 8, cy + r - 7], fill=fg, width=4)
        draw.line([cx - 8, cy + 5, cx + 9, cy - 5], fill=accent, width=4)
        for px, py, c in [(cx - 8, cy - r + 7, fg), (cx - 8, cy + r - 7, fg), (cx + 9, cy - 5, accent)]:
            draw.ellipse([px - 5, py - 5, px + 5, py + 5], fill=c)
    elif name == "shot":
        d = r - 2
        for (x0, y0), (dx, dy) in [
            ((cx - d, cy - d), (11, 11)),
            ((cx + d, cy - d), (-11, 11)),
            ((cx - d, cy + d), (11, -11)),
            ((cx + d, cy + d), (-11, -11)),
        ]:
            draw.line([x0, y0, x0 + dx, y0], fill=fg, width=4)
            draw.line([x0, y0, x0, y0 + dy], fill=fg, width=4)
        draw.ellipse([cx - r // 2, cy - r // 2, cx + r // 2, cy + r // 2], outline=fg, width=3)
        draw.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=accent)
    elif name == "folder":
        draw.rounded_rectangle([cx - r, cy - r + 5, cx, cy - r + 15], radius=4, fill=accent)
        draw.rounded_rectangle([cx - r, cy - r + 11, cx + r, cy + r - 3], radius=5, fill=fg)
    elif name == "back":
        draw.line([cx - r + 4, cy, cx + r - 4, cy], fill=fg, width=4)
        draw.line([cx - r + 4, cy, cx - 2, cy - 11], fill=fg, width=4)
        draw.line([cx - r + 4, cy, cx - 2, cy + 11], fill=fg, width=4)
    elif name == "lock":
        draw.arc([cx - r // 2, cy - r + 3, cx + r // 2, cy + 3], start=180, end=360, fill=fg, width=4)
        draw.rounded_rectangle([cx - r // 2 - 4, cy - 4, cx + r // 2 + 4, cy + r - 2], radius=5, fill=fg)
        draw.ellipse([cx - 4, cy + 3, cx + 4, cy + 11], fill=bg)
    elif name == "browser":
        draw.ellipse([cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2], outline=fg, width=4)
        draw.line([cx - r + 4, cy, cx + r - 4, cy], fill=accent, width=3)
        draw.ellipse([cx - r // 2, cy - r + 3, cx + r // 2, cy + r - 3], outline=fg, width=3)
    elif name == "cpu":
        s = r - 7
        draw.rounded_rectangle([cx - s, cy - s, cx + s, cy + s], radius=5, fill=fg)
        draw.rectangle([cx - s + 5, cy - s + 5, cx + s - 5, cy + s - 5], fill=accent)
        for i in (-8, 0, 8):
            draw.line([cx + i, cy - r, cx + i, cy - s], fill=fg, width=3)
            draw.line([cx + i, cy + s, cx + i, cy + r], fill=fg, width=3)
            draw.line([cx - r, cy + i, cx - s, cy + i], fill=fg, width=3)
            draw.line([cx + s, cy + i, cx + r, cy + i], fill=fg, width=3)
    elif name == "music":
        draw.ellipse([cx - 14, cy + 4, cx - 1, cy + 17], fill=fg)
        draw.ellipse([cx + 4, cy, cx + 17, cy + 13], fill=fg)
        draw.line([cx - 3, cy + 10, cx - 3, cy - 13], fill=fg, width=4)
        draw.line([cx + 15, cy + 6, cx + 15, cy - 17], fill=fg, width=4)
        draw.line([cx - 3, cy - 13, cx + 15, cy - 17], fill=accent, width=5)
    elif name == "clipboard":
        draw.rounded_rectangle([cx - r + 5, cy - r + 7, cx + r - 5, cy + r - 2], radius=5, fill=fg)
        draw.rounded_rectangle([cx - 8, cy - r + 2, cx + 8, cy - r + 11], radius=3, fill=accent)
    elif name == "window":
        draw.rounded_rectangle([cx - r, cy - r + 4, cx + r, cy + r - 4], radius=5, outline=fg, width=4, fill=bg)
        draw.line([cx - r + 2, cy - r + 13, cx + r - 2, cy - r + 13], fill=fg, width=3)
        draw.line([cx, cy - r + 14, cx, cy + r - 5], fill=accent, width=3)
    elif name == "flame":
        draw.polygon([(cx, cy - r + 2), (cx + r - 4, cy + 6), (cx, cy + r), (cx - r + 4, cy + 6)], fill=accent)
        draw.ellipse([cx - 6, cy, cx + 6, cy + 12], fill=fg)
    elif name == "discord":
        draw.rounded_rectangle([cx - r + 2, cy - r + 7, cx + r - 2, cy + r - 7], radius=10, fill=fg)
        draw.ellipse([cx - 10, cy - 4, cx - 3, cy + 4], fill=bg)
        draw.ellipse([cx + 3, cy - 4, cx + 10, cy + 4], fill=bg)
    elif name == "rocket":
        draw.line([cx - 10, cy + 10, cx + 12, cy - 12], fill=fg, width=7)
        draw.polygon([(cx + 3, cy - r + 3), (cx + r, cy - r + 3), (cx + r, cy - 3)], fill=accent)
        draw.ellipse([cx - 14, cy + 4, cx - 4, cy + 14], fill=accent)
    else:
        draw.polygon([(cx + 4, cy - r + 2), (cx - r // 2 - 2, cy + 3), (cx + 2, cy + 3)], fill=fg)
        draw.polygon([(cx - 2, cy - 2), (cx + r // 2 + 2, cy - 2), (cx - 5, cy + r - 2)], fill=accent)


def _draw_arc_gauge(draw: ImageDraw.ImageDraw, cx: int, cy: int, rad: int, width: int,
                    pct: float, col: tuple[int, int, int]) -> None:
    box = [cx - rad, cy - rad, cx + rad, cy + rad]
    # PIL angles: 0° = 3 o'clock clockwise -> 135° (7:30) through 270° (12:00) to 405° (4:30)
    draw.arc(box, start=135, end=405, fill=TRACK_DARK, width=width)
    if pct > 0:
        span = (270.0 * max(0.0, min(100.0, pct))) / 100.0
        draw.arc(box, start=135, end=135 + span, fill=col, width=width)


def render_cyd_frame(
    screen: str,
    config: dict[str, Any],
    stats: dict[str, Any],
    active_page: int = 0,
    vitals_sub: str = "overview",
    history: list[dict[str, float]] | None = None,
    connected: bool = True,
    pressed_tile: int = -1,
    toast: str = "",
) -> bytes:
    """Renders exact 320x240 representation matching firmware/src/display_ui.cpp and returns PNG bytes."""
    W, H = 320 * SCALE, 240 * SCALE
    img = Image.new("RGB", (W, H), BG_VOID)
    d = ImageDraw.Draw(img)

    pages = config.get("pages", [])
    if not pages:
        active_page = 0
    else:
        active_page = active_page % len(pages)

    # Top bar (0..52)
    d.rectangle([0, 0, W, 26 * SCALE], fill=BG_DOCK)
    d.line([0, 26 * SCALE - 1, W, 26 * SCALE - 1], fill=BORDER_SUB, width=2)
    dot_col = EMERALD if connected else AMBER
    d.ellipse([16, 18, 32, 34], fill=dot_col)

    scr_titles = {
        "deck": pages[active_page]["name"] if pages else "HYDRO-DECK",
        "vitals": "HW VITALS",
        "media": "MEDIA HUB",
        "timer": "CHRONO",
        "sys": "SYSTEM",
    }
    if toast:
        d.rounded_rectangle([42, 6, 310, 44], radius=10, fill=CYAN)
        d.text((176, 25), toast[:20], fill=BG_VOID, font=F_SM, anchor="mm")
    else:
        d.text((44, 26), scr_titles.get(screen, "HYDRO-DECK"), fill=TEXT_PRIMARY, font=F_SM, anchor="lm")

    clk = stats.get("clk", "--:--")
    c_pct = int(stats.get("cpu", 0))
    g_pct = int(stats.get("gpu", 0))
    d.text((W - 140, 26), f"C{c_pct}% G{g_pct}%", fill=CYAN, font=F_XS, anchor="rm")
    d.text((W - 16, 26), clk, fill=TEXT_PRIMARY, font=F_SM, anchor="rm")

    # Body
    if screen == "deck" and pages:
        pg = pages[active_page]
        tiles = pg.get("tiles", [])[:6]
        for idx in range(min(6, len(tiles))):
            t = tiles[idx]
            col_i, row_i = idx % 3, idx // 3
            tw, th = 98 * SCALE, 77 * SCALE
            tx = (8 + col_i * 103) * SCALE
            ty = (31 + row_i * 83) * SCALE
            acc = hex_rgb(t.get("col", "#00F0DA"))
            is_on = bool(t.get("tog")) and bool(t.get("on"))
            active_glow = (idx == pressed_tile) or is_on
            fill_c = BG_CARD_ALT if active_glow else BG_CARD
            rim_c = acc if active_glow else BORDER_SUB

            d.rounded_rectangle([tx, ty, tx + tw, ty + th], radius=16, fill=fill_c, outline=rim_c, width=2)
            d.rounded_rectangle([tx + 24, ty + 4, tx + tw - 24, ty + 10], radius=2, fill=acc)
            if t.get("tog"):
                if is_on:
                    d.ellipse([tx + tw - 26, ty + 14, tx + tw - 12, ty + 28], fill=acc)
                else:
                    d.ellipse([tx + tw - 26, ty + 14, tx + tw - 12, ty + 28], outline=TEXT_DIM, width=2)
            elif int(t.get("jmp", -1)) >= 0:
                d.text((tx + tw - 16, ty + 18), ">", fill=acc, font=F_XS, anchor="rm")

            icon_fg = acc if active_glow else TEXT_PRIMARY
            _draw_vector_icon(d, t.get("icon", "bolt"), tx + tw // 2, ty + 54, 48, icon_fg, fill_c, acc)
            d.text((tx + tw // 2, ty + 106), str(t.get("label", ""))[:13], fill=TEXT_PRIMARY, font=F_XS, anchor="mm")
            sub = "ACTIVE" if is_on else str(t.get("sub", ""))[:12]
            d.text((tx + tw // 2, ty + 132), sub, fill=(acc if is_on else TEXT_MUTED), font=F_XS, anchor="mm")

        # Page strip
        d.text((40, 198 * SCALE), "<", fill=TEXT_MUTED, font=F_XS, anchor="mm")
        d.text((W - 40, 198 * SCALE), ">", fill=TEXT_MUTED, font=F_XS, anchor="mm")
        start_x = (W - len(pages) * 32) // 2
        for p in range(len(pages)):
            px = start_x + p * 32
            if p == active_page:
                d.rounded_rectangle([px, 195 * SCALE, px + 28, 200 * SCALE], radius=4, fill=CYAN)
            else:
                d.ellipse([px + 8, 195 * SCALE, px + 16, 201 * SCALE], fill=BORDER_HI)

    elif screen == "vitals":
        # Sub-tabs
        sub_list = [("overview", "OVERVIEW", CYAN), ("cpu", "CPU", BLUE), ("gpu", "GPU", EMERALD), ("ram", "RAM", VIOLET)]
        for i, (sk, sl, sc) in enumerate(sub_list):
            act = vitals_sub == sk
            sx = (8 + i * 77) * SCALE
            sy = 30 * SCALE
            d.rounded_rectangle([sx, sy, sx + 72 * SCALE, sy + 19 * SCALE], radius=10,
                                fill=(sc if act else BG_CARD), outline=(sc if act else BORDER_SUB), width=2)
            d.text((sx + 36 * SCALE, sy + 10 * SCALE), sl, fill=(BG_VOID if act else TEXT_MUTED), font=F_XS, anchor="mm")

        if vitals_sub == "overview":
            cards = [
                ("CPU", f"{stats.get('logical_cores', 16)}T", c_pct, f"{stats.get('cclk', 0)/1000:.1f}GHz", str(stats.get("cproc", "CPU"))[:12], CYAN),
                ("GPU", f"{stats.get('gtmp', 0)}C", g_pct, f"{stats.get('gpwr', 0)}W", f"{stats.get('vram', 0)/1024:.1f}/{stats.get('vtot', 1024)/1024:.0f}G", EMERALD),
                ("RAM", f"{stats.get('rtot', 0):.0f}G", int(stats.get("ram", 0)), f"{stats.get('ruse', 0):.1f}G", str(stats.get("rproc", "MEM"))[:12], VIOLET),
            ]
            for idx, (title, corner, pct, sub, foot, col) in enumerate(cards):
                cx0 = (8 + idx * 103) * SCALE
                cy0 = 53 * SCALE
                cw, ch = 98 * SCALE, 98 * SCALE
                d.rounded_rectangle([cx0, cy0, cx0 + cw, cy0 + ch], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
                lc = load_color(pct, col)
                d.text((cx0 + 14, cy0 + 14), title, fill=lc, font=F_XS, anchor="lt")
                d.text((cx0 + cw - 14, cy0 + 14), corner, fill=TEXT_MUTED, font=F_XS, anchor="rt")
                _draw_arc_gauge(d, cx0 + cw // 2, cy0 + 50 * SCALE, 28 * SCALE, 12, pct, lc)
                d.text((cx0 + cw // 2, cy0 + 44 * SCALE), f"{pct}%", fill=TEXT_PRIMARY, font=F_SM, anchor="mm")
                d.text((cx0 + cw // 2, cy0 + 62 * SCALE), sub, fill=TEXT_MUTED, font=F_XS, anchor="mm")
                d.text((cx0 + cw // 2, cy0 + ch - 16), foot, fill=TEXT_PRIMARY, font=F_XS, anchor="mm")

            # Sparkline
            sx0, sy0, sw, sh = 8 * SCALE, 155 * SCALE, 304 * SCALE, 50 * SCALE
            d.rounded_rectangle([sx0, sy0, sx0 + sw, sy0 + sh], radius=14, fill=BG_SURFACE, outline=BORDER_SUB, width=2)
            d.line([sx0 + 10, sy0 + 24 * SCALE, sx0 + sw - 10, sy0 + 24 * SCALE], fill=TRACK_DARK, width=2)
            pts_src = (history or [])[-48:]
            if len(pts_src) >= 2:
                step_x = (sw - 24) / max(1, len(pts_src) - 1)
                for key, lc in [("ram", VIOLET), ("gpu", EMERALD), ("cpu", CYAN)]:
                    pts = [(sx0 + 12 + i * step_x, sy0 + sh - 10 - (min(100, pts_src[i].get(key, 0)) / 100.0) * (sh - 24)) for i in range(len(pts_src))]
                    d.line(pts, fill=lc, width=3)
            d.text((sx0 + 14, sy0 + 8), "48s HISTORY", fill=TEXT_MUTED, font=F_XS, anchor="lt")
            d.text((sx0 + sw - 14, sy0 + 8), f"D:{stats.get('ndn', 0):.1f} U:{stats.get('nup', 0):.1f}M", fill=TEXT_MUTED, font=F_XS, anchor="rt")

        elif vitals_sub == "cpu":
            d.rounded_rectangle([16, 106, 232, 410], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            lc = load_color(c_pct, BLUE)
            _draw_arc_gauge(d, 124, 224, 54, 12, c_pct, lc)
            d.text((124, 218), f"{c_pct}%", fill=TEXT_PRIMARY, font=F_MD, anchor="mm")
            d.text((124, 258), f"{stats.get('cclk', 0)/1000:.2f} GHz", fill=TEXT_MUTED, font=F_XS, anchor="mm")
            d.text((124, 384), str(stats.get("cpun", "CPU"))[:16], fill=TEXT_PRIMARY, font=F_XS, anchor="mm")

            d.rounded_rectangle([244, 106, 624, 410], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            d.text((264, 120), "LOGICAL CORES LOAD", fill=CYAN, font=F_XS, anchor="lt")
            cores = stats.get("cores", [c_pct] * 16)[:16]
            for i, cv in enumerate(cores):
                col_c, row_c = i // 8, i % 8
                bx, by = 264 + col_c * 180, 152 + row_c * 30
                d.text((bx, by + 3), f"C{i}", fill=TEXT_MUTED, font=F_XS, anchor="lt")
                d.rounded_rectangle([bx + 44, by + 4, bx + 140, by + 18], radius=6, fill=TRACK_DARK)
                fw = int(96 * min(100, cv) / 100)
                if fw > 0:
                    d.rounded_rectangle([bx + 44, by + 4, bx + 44 + max(6, fw), by + 18], radius=6, fill=load_color(cv, CYAN))

        elif vitals_sub == "gpu":
            d.rounded_rectangle([16, 106, 232, 410], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            lc = load_color(g_pct, EMERALD)
            _draw_arc_gauge(d, 124, 224, 54, 12, g_pct, lc)
            d.text((124, 218), f"{g_pct}%", fill=TEXT_PRIMARY, font=F_MD, anchor="mm")
            d.text((124, 258), f"{stats.get('gpwr', 0)} W", fill=TEXT_MUTED, font=F_XS, anchor="mm")
            d.text((124, 384), f"FAN {stats.get('gfan', 0)}%", fill=TEXT_PRIMARY, font=F_XS, anchor="mm")

            d.rounded_rectangle([244, 106, 624, 410], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            d.text((264, 120), str(stats.get("gpun", "GPU"))[:24], fill=EMERALD, font=F_XS, anchor="lt")
            bars = [
                ("BOARD POWER", f"{stats.get('gpwr',0)} / {stats.get('gpmx',450)} W", stats.get("gpwr", 0) / max(1, stats.get("gpmx", 450)), AMBER),
                ("VRAM ALLOC", f"{stats.get('vram',0)/1024:.1f} / {stats.get('vtot',24564)/1024:.1f} GB", stats.get("vram", 0) / max(1, stats.get("vtot", 1)), VIOLET),
                ("CORE / MEM CLK", f"{stats.get('gclk',0)} / {stats.get('gmck',0)} MHz", min(1.0, stats.get("gclk", 0) / 2800.0), CYAN),
                ("THERMAL / FAN", f"{stats.get('gtmp',0)}C FAN {stats.get('gfan',0)}%", min(1.0, stats.get("gtmp", 0) / 95.0), EMERALD),
            ]
            for bi, (lbl, val, rat, bcol) in enumerate(bars):
                yy = 156 + bi * 56
                d.text((264, yy), lbl, fill=TEXT_MUTED, font=F_XS, anchor="lt")
                d.text((604, yy), val, fill=TEXT_PRIMARY, font=F_XS, anchor="rt")
                d.rounded_rectangle([264, yy + 24, 604, yy + 38], radius=7, fill=TRACK_DARK)
                fw = int(340 * max(0.0, min(1.0, rat)))
                if fw > 0:
                    d.rounded_rectangle([264, yy + 24, 264 + max(8, fw), yy + 38], radius=7, fill=bcol)

        else:  # ram
            r_pct = int(stats.get("ram", 0))
            d.rounded_rectangle([16, 106, 232, 410], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            _draw_arc_gauge(d, 124, 224, 54, 12, r_pct, load_color(r_pct, VIOLET))
            d.text((124, 218), f"{r_pct}%", fill=TEXT_PRIMARY, font=F_MD, anchor="mm")
            d.text((124, 258), f"{stats.get('ruse',0):.1f}G", fill=TEXT_MUTED, font=F_XS, anchor="mm")
            d.text((124, 384), str(stats.get("rproc", "MEM"))[:15], fill=TEXT_PRIMARY, font=F_XS, anchor="mm")

            d.rounded_rectangle([244, 106, 624, 410], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            d.text((264, 120), "MEMORY & SYSTEM I/O", fill=VIOLET, font=F_XS, anchor="lt")
            rows = [
                ("USED / TOTAL", f"{stats.get('ruse',0):.1f} / {stats.get('rtot',96):.1f} GB", TEXT_PRIMARY),
                ("TOP RAM APP", str(stats.get("rproc", "-")), VIOLET),
                ("NET DOWN", f"{stats.get('ndn',0):.1f} Mbps", CYAN),
                ("NET UP", f"{stats.get('nup',0):.1f} Mbps", EMERALD),
                ("SYSTEM DISK", f"{stats.get('dfre',0):.0f} GB FREE", AMBER),
                ("HOST", str(stats.get("host", "-")), TEXT_MUTED),
            ]
            for ri, (rk, rv, rcol) in enumerate(rows):
                yy = 162 + ri * 40
                d.text((264, yy), rk, fill=TEXT_MUTED, font=F_XS, anchor="lt")
                d.text((604, yy), rv, fill=rcol, font=F_XS, anchor="rt")

    elif screen == "media":
        d.rounded_rectangle([16, 64, 624, 180], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
        playing = bool(stats.get("mplay"))
        d.text((40, 80), "NOW PLAYING // LIVE" if playing else "MEDIA TRANSPORT", fill=CYAN, font=F_XS, anchor="lt")
        d.text((40, 122), str(stats.get("song", "PC Audio Ready"))[:28], fill=TEXT_PRIMARY, font=F_SM, anchor="lt")
        for bi in range(6):
            bh = (10 + ((bi * 7) % 18)) if playing else 8
            d.rectangle([536 + bi * 12, 112 - bh, 544 + bi * 12, 112], fill=(EMERALD if playing else TEXT_DIM))

        btns = [("prev", "PREV", BLUE), ("pause" if playing else "play", "PAUSE" if playing else "PLAY", EMERALD), ("next", "NEXT", BLUE)]
        for i, (ico, lbl, col) in enumerate(btns):
            bx = (8 + i * 103) * SCALE
            d.rounded_rectangle([bx, 196, bx + 196, 292], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
            _draw_vector_icon(d, ico, bx + 60, 244, 42, col, BG_CARD, CYAN)
            d.text((bx + 104, 244), lbl, fill=TEXT_PRIMARY, font=F_XS, anchor="lm")

        vol = int(stats.get("vol", 50))
        vmute = bool(stats.get("vmute"))
        d.rounded_rectangle([16, 308, 412, 404], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
        d.text((92, 324), "VOLUME [MUTED]" if vmute else "MASTER VOLUME", fill=TEXT_MUTED, font=F_XS, anchor="lt")
        d.text((392, 322), f"{vol}%", fill=CYAN, font=F_SM, anchor="rt")
        _draw_vector_icon(d, "mute" if vmute else "vol_up", 56, 356, 38, CRIMSON if vmute else CYAN, BG_CARD, CRIMSON)
        d.rounded_rectangle([92, 368, 392, 384], radius=8, fill=TRACK_DARK)
        vfw = int(300 * vol / 100)
        if vfw > 0:
            d.rounded_rectangle([92, 368, 92 + max(12, vfw), 384], radius=8, fill=(CRIMSON if vmute else CYAN))
        d.ellipse([92 + vfw - 11, 365, 92 + vfw + 11, 387], fill=TEXT_PRIMARY)

        mmute = bool(stats.get("mmute"))
        mcol = CRIMSON if mmute else EMERALD
        d.rounded_rectangle([428, 308, 624, 404], radius=16, fill=(BG_CARD_ALT if mmute else BG_CARD), outline=mcol, width=2)
        _draw_vector_icon(d, "mic_off" if mmute else "mic", 478, 356, 40, mcol, BG_CARD, CRIMSON)
        d.text((520, 356), "MUTED" if mmute else "LIVE", fill=mcol, font=F_SM, anchor="lm")

    elif screen == "timer":
        d.rounded_rectangle([16, 68, 288, 404], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
        d.arc([56, 114, 248, 306], start=0, end=360, fill=TRACK_DARK, width=16)
        d.arc([56, 114, 248, 306], start=-90, end=120, fill=CYAN, width=16)
        d.text((152, 200), "25:00", fill=TEXT_PRIMARY, font=F_LG, anchor="mm")
        d.text((152, 242), "READY", fill=CYAN, font=F_XS, anchor="mm")
        for i, pl in enumerate(["5m", "15m", "25m", "STPW"]):
            px = 304 + i * 80
            sel = pl == "25m"
            d.rounded_rectangle([px, 68, px + 74, 156], radius=12, fill=(BG_CARD_ALT if sel else BG_CARD), outline=(CYAN if sel else BORDER_SUB), width=2)
            d.text((px + 37, 112), pl, fill=(CYAN if sel else TEXT_PRIMARY), font=F_XS, anchor="mm")
        d.rounded_rectangle([304, 176, 624, 284], radius=16, fill=BG_CARD_ALT, outline=EMERALD, width=3)
        d.text((464, 230), "START TIMER", fill=EMERALD, font=F_SM, anchor="mm")
        d.rounded_rectangle([304, 300, 624, 404], radius=16, fill=BG_CARD, outline=BORDER_HI, width=2)
        d.text((464, 352), "RESET CLOCK", fill=TEXT_PRIMARY, font=F_SM, anchor="mm")

    elif screen == "sys":
        ds = config.get("device_settings", {})
        bl = int(ds.get("bl", 230))
        bl_pct = int(round((bl - 25) / 230 * 90 + 10))
        d.rounded_rectangle([16, 64, 624, 136], radius=16, fill=BG_CARD, outline=BORDER_SUB, width=2)
        d.text((36, 78), f"BACKLIGHT: {bl_pct}%", fill=TEXT_PRIMARY, font=F_XS, anchor="lt")
        d.rounded_rectangle([36, 106, 604, 120], radius=6, fill=TRACK_DARK)
        d.rounded_rectangle([36, 106, 36 + int(568 * bl / 255), 120], radius=6, fill=CYAN)
        leds = ["VITALS", "CYCLE", "CYAN", "OFF"]
        slp = int(ds.get("slp", 300))
        cards = [
            (f"RGB: {leds[int(ds.get('led', 0)) % 4]}", VIOLET),
            (f"AUDIO: {'ON' if ds.get('sfx', True) else 'OFF'}", EMERALD if ds.get('sfx', True) else TEXT_MUTED),
            ("SLEEP: NEVER" if slp == 0 else f"SLEEP: {slp // 60}m", BLUE),
            (f"FLIP: ROT {ds.get('rot', 1)}", CYAN),
            (f"INV:{int(bool(ds.get('inv')))} BGR:{int(bool(ds.get('bgr')))}", AMBER),
            ("CALIBRATE TOUCH", CRIMSON),
        ]
        for ci, (clbl, ccol) in enumerate(cards):
            cc, cr = ci % 2, ci // 2
            cx0, cy0 = 16 + cc * 310, 148 + cr * 86
            d.rounded_rectangle([cx0, cy0, cx0 + 298, cy0 + 76], radius=14, fill=BG_CARD, outline=ccol, width=2)
            d.text((cx0 + 149, cy0 + 38), clbl, fill=TEXT_PRIMARY, font=F_XS, anchor="mm")

    # Bottom Navigation Dock (210..240 -> 420..480)
    dy0 = 210 * SCALE
    d.rectangle([0, dy0, W, H], fill=BG_DOCK)
    d.line([0, dy0, W, dy0], fill=BORDER_SUB, width=2)
    tabs = [("deck", "DECK"), ("vitals", "VITALS"), ("media", "MEDIA"), ("timer", "TIMER"), ("sys", "SYS")]
    for i, (sk, sl) in enumerate(tabs):
        tx0 = i * 64 * SCALE
        act = screen == sk
        if act:
            d.rounded_rectangle([tx0 + 8, dy0 + 6, tx0 + 120, H - 6], radius=12, fill=BG_CARD)
            d.rectangle([tx0 + 28, dy0 + 2, tx0 + 100, dy0 + 6], fill=CYAN)
        d.text((tx0 + 64, dy0 + 30), sl, fill=(CYAN if act else TEXT_MUTED), font=(F_SM if act else F_XS), anchor="mm")

    out_img = img.resize((320, 240), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    out_img.save(buf, format="PNG")
    return buf.getvalue()
