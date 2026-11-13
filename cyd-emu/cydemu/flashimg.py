"""The virtual board's SPI flash chip and eFuse block.

"Flashing" here means the same thing it means on hardware: bytes are written into the flash
image at an offset. Whatever is not overwritten stays, so NVS / LittleFS / SPIFFS contents
survive a re-flash exactly as they do on the real board.
"""
from __future__ import annotations

import hashlib
import json
import struct
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULTS = ROOT / "defaults" / "arduino-esp32"

OFF_BOOTLOADER = 0x1000
OFF_PARTITIONS = 0x8000
OFF_OTADATA = 0xE000
OFF_APP = 0x10000
APP_DESC_MAGIC = 0xABCD5432
DEFAULT_MAC = "24:0a:c4:c1:d0:01"


class FlashError(Exception):
    pass


# ----------------------------------------------------------------- classification

def classify(data: bytes) -> str:
    """What kind of ESP32 binary is this? -> merged | app | bootloader | partitions | unknown"""
    if len(data) > OFF_PARTITIONS + 2 and data[OFF_BOOTLOADER] == 0xE9 and data[OFF_PARTITIONS:OFF_PARTITIONS + 2] == b"\xaa\x50":
        return "merged"
    if data[:2] == b"\xaa\x50":
        return "partitions"
    if data[:1] == b"\xe9":
        if len(data) >= 0x24 and struct.unpack_from("<I", data, 0x20)[0] == APP_DESC_MAGIC:
            return "app"
        return "bootloader"
    return "unknown"


def app_info(data: bytes, offset: int = 0) -> dict | None:
    """Read esp_app_desc_t from an application image."""
    if len(data) < offset + 0x120 or data[offset] != 0xE9:
        return None
    if struct.unpack_from("<I", data, offset + 0x20)[0] != APP_DESC_MAGIC:
        return None

    def s(off: int, n: int) -> str:
        return data[offset + off:offset + off + n].split(b"\0", 1)[0].decode("utf-8", "replace")

    return {
        "version": s(0x30, 32),
        "project": s(0x50, 32),
        "compile_time": s(0x70, 16),
        "compile_date": s(0x80, 16),
        "idf": s(0x90, 32),
    }


def parse_partitions(data: bytes) -> list[dict]:
    out = []
    for i in range(0, min(len(data), 0xC00), 32):
        e = data[i:i + 32]
        if len(e) < 32 or e[:2] != b"\xaa\x50":
            break
        off, size = struct.unpack_from("<II", e, 4)
        out.append({"name": e[12:28].split(b"\0", 1)[0].decode("ascii", "replace"),
                    "type": e[2], "subtype": e[3], "offset": off, "size": size})
    return out


_SUBTYPES = {
    "app": {"factory": 0x00, "test": 0x20, **{f"ota_{i}": 0x10 + i for i in range(16)}},
    "data": {"ota": 0x00, "phy": 0x01, "nvs": 0x02, "coredump": 0x03, "nvs_keys": 0x04,
             "efuse": 0x05, "esphttpd": 0x80, "fat": 0x81, "spiffs": 0x82, "littlefs": 0x83},
}


def _num(text: str) -> int:
    text = text.strip()
    mult = 1
    if text[-1:] in "kK":
        mult, text = 1024, text[:-1]
    elif text[-1:] in "mM":
        mult, text = 1024 * 1024, text[:-1]
    return int(text, 0) * mult


def partitions_from_csv(text: str) -> bytes:
    """Build a binary partition table from an ESP-IDF / Arduino partitions.csv."""
    entries = b""
    cursor = 0x9000
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        cols += [""] * (6 - len(cols))
        name, ptype, subtype, offset, size, flags = cols[:6]
        tnum = {"app": 0, "data": 1}.get(ptype.lower())
        if tnum is None:
            tnum = int(ptype, 0)
        snum = _SUBTYPES.get(ptype.lower(), {}).get(subtype.lower())
        if snum is None:
            snum = int(subtype, 0)
        align = 0x10000 if tnum == 0 else 0x1000
        off = _num(offset) if offset else (cursor + align - 1) // align * align
        sz = _num(size)
        cursor = off + sz
        fl = 1 if "encrypted" in flags.lower() else 0
        entries += struct.pack("<2sBBII16sI", b"\xaa\x50", tnum, snum, off, sz, name.encode()[:16], fl)
    md5 = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    table = entries + md5
    return table + b"\xff" * (0xC00 - len(table))


# ----------------------------------------------------------------- the chip

class FlashChip:
    def __init__(self, state_dir: Path, size: int = 4 * 1024 * 1024):
        self.dir = Path(state_dir)
        self.size = size
        self.path = self.dir / "flash.bin"
        self.efuse_path = self.dir / "efuse.bin"
        self.meta_path = self.dir / "flash.json"

    # -- persistence
    def exists(self) -> bool:
        return self.path.exists() and self.path.stat().st_size == self.size

    def read(self) -> bytearray:
        if not self.exists():
            return bytearray(b"\xff" * self.size)
        return bytearray(self.path.read_bytes())

    def _write(self, data: bytearray) -> None:
        self.dir.mkdir(parents=True, exist_ok=True)
        if len(data) != self.size:
            raise FlashError("internal: flash image has the wrong size")
        self.path.write_bytes(data)

    def meta(self) -> dict:
        try:
            return json.loads(self.meta_path.read_text("utf-8"))
        except (OSError, ValueError):
            return {}

    def _save_meta(self, meta: dict) -> None:
        self.dir.mkdir(parents=True, exist_ok=True)
        self.meta_path.write_text(json.dumps(meta, indent=2), "utf-8")

    # -- eFuse (holds the MAC address and chip revision)
    def ensure_efuse(self, mac: str = DEFAULT_MAC) -> None:
        if self.efuse_path.exists() and self.efuse_path.stat().st_size == 124:
            return
        raw = bytes.fromhex(mac.replace(":", "").replace("-", ""))
        if len(raw) != 6:
            raise FlashError(f"bad MAC address: {mac}")
        crc = 0
        for b in raw:
            crc ^= b
            for _ in range(8):
                crc = ((crc >> 1) ^ 0x8C) & 0xFF if crc & 1 else crc >> 1
        ef = bytearray(124)
        ef[13] = 0x80
        ef[22] = 0x10
        ef[4:10] = raw[::-1]
        ef[10] = crc
        self.dir.mkdir(parents=True, exist_ok=True)
        self.efuse_path.write_bytes(ef)

    # -- operations
    def erase(self) -> dict:
        """Full chip erase (every byte 0xFF). The board will not boot until something is flashed."""
        data = bytearray(b"\xff" * self.size)
        self._write(data)
        self._save_meta({"erased": time.strftime("%Y-%m-%d %H:%M:%S"), "writes": []})
        return self.describe()

    def write_at(self, offset: int, blob: bytes, label: str, source: str = "") -> None:
        if offset < 0 or offset + len(blob) > self.size:
            raise FlashError(f"{label}: {len(blob)} bytes at {offset:#x} does not fit in a {self.size // 1024 // 1024} MB flash")
        data = self.read()
        # NOR flash is erased a 4 KiB sector at a time before programming
        end = (offset + len(blob) + 0xFFF) & ~0xFFF
        data[offset:end] = b"\xff" * (end - offset)
        data[offset:offset + len(blob)] = blob
        self._write(data)
        meta = self.meta()
        writes = [w for w in meta.get("writes", []) if w.get("offset") != offset]
        writes.append({"offset": offset, "size": len(blob), "label": label, "source": source,
                       "sha256": hashlib.sha256(blob).hexdigest()[:16],
                       "time": time.strftime("%Y-%m-%d %H:%M:%S")})
        meta["writes"] = sorted(writes, key=lambda w: w["offset"])
        self._save_meta(meta)

    def flash(self, path: str | Path, offset: int | None = None, bootloader: str | None = None,
              partitions: str | None = None, boot_app0: str | None = None, erase: bool = False) -> dict:
        """Flash a firmware. Accepts a merged image, an app .bin, or a build directory."""
        src = Path(path)
        if not src.exists():
            raise FlashError(f"no such file or directory: {src}")
        plan = _plan(src, offset, bootloader, partitions, boot_app0)
        if erase:
            self.erase()
        current = self.read()
        if not any(off == 0 for off, _, _, _ in plan):
            # like a board that has never been flashed over USB: make sure it can boot an app
            have = {off for off, _, _, _ in plan}
            if OFF_BOOTLOADER not in have and current[OFF_BOOTLOADER] != 0xE9:
                plan.append((OFF_BOOTLOADER, (DEFAULTS / "bootloader_dio_40m.bin").read_bytes(), "bootloader", "default arduino-esp32 bootloader"))
            if OFF_PARTITIONS not in have and current[OFF_PARTITIONS:OFF_PARTITIONS + 2] != b"\xaa\x50":
                plan.append((OFF_PARTITIONS, (DEFAULTS / "partitions_huge_app.bin").read_bytes(), "partitions", "default huge_app partition table"))
                if OFF_OTADATA not in have:
                    plan.append((OFF_OTADATA, (DEFAULTS / "boot_app0.bin").read_bytes(), "otadata", "default boot_app0"))
        for off, blob, label, source in sorted(plan, key=lambda p: p[0]):
            self.write_at(off, blob, label, source)
        return self.describe()

    def describe(self) -> dict:
        data = self.read()
        parts = parse_partitions(bytes(data[OFF_PARTITIONS:OFF_PARTITIONS + 0xC00]))
        apps = []
        for p in parts:
            if p["type"] == 0:
                info = app_info(bytes(data[p["offset"]:p["offset"] + 0x120]))
                if info:
                    apps.append({"partition": p["name"], "offset": p["offset"], **info})
        return {
            "path": str(self.path),
            "size": self.size,
            "bootloader": data[OFF_BOOTLOADER] == 0xE9,
            "partitions": parts,
            "apps": apps,
            "blank": not any(b != 0xFF for b in data[OFF_BOOTLOADER:OFF_BOOTLOADER + 16]),
            "writes": self.meta().get("writes", []),
        }


def _load_partitions(path: Path) -> bytes:
    if path.suffix.lower() == ".csv":
        return partitions_from_csv(path.read_text("utf-8"))
    return path.read_bytes()


def _sibling(app: Path, kind: str) -> Path | None:
    """Find the bootloader / partition table that was built alongside an app image."""
    d, name = app.parent, app.name
    candidates = [
        d / f"{kind}.bin",                                  # PlatformIO: firmware.bin + bootloader.bin
        d / name.replace("firmware", kind),                 # X_firmware.bin -> X_bootloader.bin
        d / (app.stem + f".{kind}.bin"),                    # Arduino IDE: sketch.ino.bin -> sketch.ino.bootloader.bin
        d / name.replace(".bin", f".{kind}.bin"),
        d / "bootloader" / f"{kind}.bin",                   # ESP-IDF build dir
        d / "partition_table" / "partition-table.bin" if kind == "partitions" else d / f"{kind}.bin",
    ]
    for c in candidates:
        if c != app and c.is_file():
            return c
    return None


def _plan(src: Path, offset, bootloader, partitions, boot_app0) -> list[tuple[int, bytes, str, str]]:
    plan: list[tuple[int, bytes, str, str]] = []
    app: Path | None = None

    if src.is_dir():
        for name in ("firmware.bin", "firmware.factory.bin"):
            if (src / name).is_file():
                app = src / name
                break
        if app is None:
            bins = [p for p in sorted(src.glob("*.bin")) if classify(p.read_bytes()[:0x9000]) in ("app", "merged")]
            if not bins:
                raise FlashError(f"no application .bin found in {src}")
            app = bins[0]
    else:
        app = src

    blob = app.read_bytes()
    kind = classify(blob)
    if offset is not None:
        plan.append((offset, blob, kind if kind != "unknown" else "raw", str(app)))
    elif kind == "merged":
        plan.append((0, blob, "merged image", str(app)))
    elif kind == "app":
        plan.append((OFF_APP, blob, "app", str(app)))
    elif kind == "bootloader":
        plan.append((OFF_BOOTLOADER, blob, "bootloader", str(app)))
    elif kind == "partitions":
        plan.append((OFF_PARTITIONS, blob, "partitions", str(app)))
    else:
        raise FlashError(f"{app.name} is not an ESP32 image (no 0xE9 header). Pass --offset to write raw data.")

    if kind == "app" and offset is None:
        bl = Path(bootloader) if bootloader else _sibling(app, "bootloader")
        pt = Path(partitions) if partitions else _sibling(app, "partitions")
        ba = Path(boot_app0) if boot_app0 else next((p for p in (app.parent / "boot_app0.bin",) if p.is_file()), None)
        if bl:
            plan.append((OFF_BOOTLOADER, bl.read_bytes(), "bootloader", str(bl)))
        if pt:
            table = _load_partitions(pt)
            plan.append((OFF_PARTITIONS, table, "partitions", str(pt)))
            app_parts = [p for p in parse_partitions(table) if p["type"] == 0]
            if app_parts:
                first = min(app_parts, key=lambda p: p["offset"])
                if first["offset"] != OFF_APP:
                    plan[0] = (first["offset"], blob, "app", str(app))
                if len(blob) > first["size"]:
                    raise FlashError(f"app is {len(blob)} bytes but partition '{first['name']}' is only {first['size']} bytes")
        if ba:
            plan.append((OFF_OTADATA, ba.read_bytes(), "otadata", str(ba)))
    else:
        if bootloader:
            plan.append((OFF_BOOTLOADER, Path(bootloader).read_bytes(), "bootloader", bootloader))
        if partitions:
            plan.append((OFF_PARTITIONS, _load_partitions(Path(partitions)), "partitions", partitions))
        if boot_app0:
            plan.append((OFF_OTADATA, Path(boot_app0).read_bytes(), "otadata", boot_app0))
    return plan
