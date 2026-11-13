from __future__ import annotations

import io
import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from backend.config_store import ConfigStore
from backend.cyd_simulator import render_cyd_frame
from backend.telemetry import TelemetryCollector

OUT_DIR = Path(__file__).resolve().parent / "rendered_screens"


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    cfg = ConfigStore().config
    tc = TelemetryCollector()
    stats = tc.sample()
    # Populate realistic multi-sample curve for visual sparkline inspection
    hist = []
    for i in range(48):
        hist.append({
            "cpu": max(4, min(95, int(stats["cpu"] + ((i % 7) - 3) * 4))),
            "gpu": max(2, min(95, int(stats["gpu"] + ((i % 5) - 2) * 5))),
            "ram": stats["ram"],
        })

    views = [
        ("deck_main", "deck", 0, "overview", "MAIN DECK (3x2)"),
        ("deck_dev", "deck", 1, "overview", "DEV LAB FOLDER"),
        ("vitals_all", "vitals", 0, "overview", "HW VITALS OVERVIEW"),
        ("vitals_cpu", "vitals", 0, "cpu", "CPU CORES DETAIL"),
        ("vitals_gpu", "vitals", 0, "gpu", "RTX 4090 DETAIL"),
        ("vitals_ram", "vitals", 0, "ram", "RAM & I/O DETAIL"),
        ("media", "media", 0, "overview", "MEDIA & MIC HUB"),
        ("timer", "timer", 0, "overview", "POMODORO TIMER"),
        ("sys", "sys", 0, "overview", "SYSTEM CONTROLS"),
    ]

    imgs: list[tuple[str, Image.Image]] = []
    for slug, scr, pg, sub, label in views:
        png = render_cyd_frame(
            screen=scr,
            config=cfg,
            stats=stats,
            active_page=pg,
            vitals_sub=sub,
            history=hist,
            connected=True,
        )
        p = OUT_DIR / f"cyd_{slug}.png"
        p.write_bytes(png)
        imgs.append((label, Image.open(io.BytesIO(png)).convert("RGB")))

    # Composite 3x3 showcase sheet (each tile padded neatly on obsidian backdrop)
    pad = 18
    hdr = 26
    tw, th = 320, 240
    cols, rows = 3, 3
    sheet_w = cols * tw + (cols + 1) * pad
    sheet_h = rows * (th + hdr) + (rows + 1) * pad + 36
    sheet = Image.new("RGB", (sheet_w, sheet_h), (6, 8, 13))
    draw = ImageDraw.Draw(sheet)
    try:
        f_title = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 20)
        f_sub = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 13)
    except Exception:
        f_title = ImageFont.load_default()
        f_sub = ImageFont.load_default()

    draw.text((pad, 12), "HYDRO-DECK // 2.8\" E32R28T (320x240) — ALL 9 HARDWARE SCREENS", fill=(0, 240, 218), font=f_title)
    for idx, (label, im) in enumerate(imgs):
        r, c = idx // cols, idx % cols
        x = pad + c * (tw + pad)
        y = 44 + pad + r * (th + hdr + pad)
        draw.text((x, y), label, fill=(180, 200, 230), font=f_sub)
        draw.rounded_rectangle([x - 2, y + 20, x + tw + 1, y + 22 + th + 1], radius=6, outline=(36, 52, 78), width=2)
        sheet.paste(im, (x, y + 22))

    sheet_path = OUT_DIR / "cyd_all_screens_sheet.png"
    sheet.save(sheet_path)
    print(f"Rendered {len(imgs)} CYD frames + composite sheet: {sheet_path}")


if __name__ == "__main__":
    main()

