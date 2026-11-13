"""Flash-chip behaviour: image classification, partition tables, and what a re-flash preserves."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from cydemu.flashimg import (  # noqa: E402
    OFF_APP, OFF_BOOTLOADER, OFF_PARTITIONS, FlashChip, FlashError, app_info, classify,
    parse_partitions, partitions_from_csv,
)

DIST = ROOT.parent / "firmware_dist"
APP = DIST / "HYDRO-DECK_E32R28T_ILI9341_firmware.bin"
BOOTLOADER = DIST / "HYDRO-DECK_E32R28T_ILI9341_bootloader.bin"
PARTITIONS = DIST / "HYDRO-DECK_E32R28T_ILI9341_partitions.bin"
MERGED = ROOT.parent / "Freenove_ESP32_Display-main" / "NerdMiner-FNK0103" / "Bin" / "FNK0103F" / "merged_output.bin"
HUGE_APP_CSV = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/huge_app.csv"

needs_dist = pytest.mark.skipif(not APP.exists(), reason="firmware_dist images not present")


@needs_dist
def test_classify_recognises_each_image_kind():
    assert classify(APP.read_bytes()) == "app"
    assert classify(BOOTLOADER.read_bytes()) == "bootloader"
    assert classify(PARTITIONS.read_bytes()) == "partitions"
    assert classify(b"hello world, not firmware") == "unknown"
    if MERGED.exists():
        assert classify(MERGED.read_bytes()) == "merged"


@needs_dist
def test_app_info_reads_the_app_descriptor():
    info = app_info(APP.read_bytes())
    assert info is not None
    assert info["idf"].startswith("v4.4")
    assert app_info(BOOTLOADER.read_bytes()) is None


@pytest.mark.skipif(not (HUGE_APP_CSV.exists() and PARTITIONS.exists()), reason="needs PlatformIO's huge_app.csv")
def test_partition_csv_compiles_to_the_same_table_platformio_built():
    built = partitions_from_csv(HUGE_APP_CSV.read_text("utf-8"))
    assert built == PARTITIONS.read_bytes()


@needs_dist
def test_flashing_an_app_brings_its_sibling_bootloader_and_partitions(tmp_path):
    chip = FlashChip(tmp_path)
    desc = chip.flash(APP)
    data = chip.read()
    assert data[OFF_BOOTLOADER:OFF_BOOTLOADER + 64] == BOOTLOADER.read_bytes()[:64]
    assert data[OFF_PARTITIONS:OFF_PARTITIONS + 64] == PARTITIONS.read_bytes()[:64]
    assert data[OFF_APP:OFF_APP + 4096] == APP.read_bytes()[:4096]
    assert [p["name"] for p in desc["partitions"]][:3] == ["nvs", "otadata", "app0"]
    assert desc["apps"] and desc["apps"][0]["partition"] == "app0"


@needs_dist
def test_reflashing_the_app_keeps_nvs_and_filesystem_like_real_hardware(tmp_path):
    chip = FlashChip(tmp_path)
    chip.flash(APP)
    nvs = next(p for p in parse_partitions(PARTITIONS.read_bytes()) if p["name"] == "nvs")
    fs = next(p for p in parse_partitions(PARTITIONS.read_bytes()) if p["name"] == "spiffs")
    chip.write_at(nvs["offset"], b"SAVED-SETTINGS", "test")
    chip.write_at(fs["offset"], b"SAVED-FILES", "test")

    chip.flash(APP)
    data = chip.read()
    assert data[nvs["offset"]:nvs["offset"] + 14] == b"SAVED-SETTINGS"
    assert data[fs["offset"]:fs["offset"] + 11] == b"SAVED-FILES"

    chip.flash(APP, erase=True)
    data = chip.read()
    assert data[nvs["offset"]:nvs["offset"] + 14] == b"\xff" * 14


@needs_dist
def test_app_alone_on_a_blank_chip_gets_a_default_bootloader(tmp_path):
    lone = tmp_path / "src" / "only_app.bin"
    lone.parent.mkdir()
    lone.write_bytes(APP.read_bytes())
    chip = FlashChip(tmp_path / "state")
    desc = chip.flash(lone)
    assert desc["bootloader"] is True
    assert any(p["name"] == "app0" for p in desc["partitions"])


def test_non_firmware_is_rejected_unless_an_offset_is_given(tmp_path):
    junk = tmp_path / "notes.bin"
    junk.write_bytes(b"not an esp32 image")
    chip = FlashChip(tmp_path / "state")
    with pytest.raises(FlashError):
        chip.flash(junk)
    chip.flash(junk, offset=0x310000)
    assert chip.read()[0x310000:0x310000 + 18] == b"not an esp32 image"


def test_image_that_does_not_fit_is_rejected(tmp_path):
    chip = FlashChip(tmp_path)
    with pytest.raises(FlashError):
        chip.write_at(chip.size - 4, b"12345678", "too big")
