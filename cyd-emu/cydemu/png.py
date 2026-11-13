"""Minimal PNG encoder / decoder helpers (stdlib only)."""
from __future__ import annotations

import struct
import zlib


def scale_rgb(w: int, h: int, rgb: bytes, scale: int) -> tuple[int, int, bytes]:
    if scale <= 1:
        return w, h, rgb
    rows = []
    stride = w * 3
    for y in range(h):
        row = rgb[y * stride:(y + 1) * stride]
        wide = b"".join(row[i:i + 3] * scale for i in range(0, stride, 3))
        rows.append(wide * scale)
    return w * scale, h * scale, b"".join(rows)


def encode_png(w: int, h: int, rgb: bytes, scale: int = 1) -> bytes:
    w, h, rgb = scale_rgb(w, h, rgb, scale)
    stride = w * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(h))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
