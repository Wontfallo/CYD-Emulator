from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from fastapi.testclient import TestClient
from backend.config_store import ConfigStore
from backend.macro_engine import MacroEngine, parse_hotkey
from backend.telemetry import TelemetryCollector
from backend.cyd_simulator import render_cyd_frame
from backend.app import app


def test_real_telemetry_collection():
    tc = TelemetryCollector()
    stats = tc.sample()
    assert isinstance(stats["cpu"], int) and 0 <= stats["cpu"] <= 100
    assert stats["cclk"] >= 400
    assert 1 <= len(stats["cores"]) <= 16
    assert stats["rtot"] >= 16.0
    if stats["vtot"] > 0:
        assert stats["vtot"] > 4000, f"Expected GPU VRAM > 4GB, got {stats['vtot']}"
        assert stats["gtmp"] > 0
    else:
        assert stats["gtmp"] == -1
    pkt = tc.to_device_packet(stats)
    assert pkt["t"] == "stat"


def test_config_store_roundtrip(tmp_path: Path):
    cfg_path = tmp_path / "cfg.json"
    cs = ConfigStore(cfg_path)
    assert len(cs.config["pages"]) == 4
    exported = cs.export_json()
    cs2 = ConfigStore(tmp_path / "cfg2.json")
    cs2.import_json(exported)
    assert cs2.config["pages"][0]["name"] == "MAIN"
    pkt = cs2.to_device_deck_packet(0)
    assert pkt["t"] == "deck"
    assert len(pkt["pages"][0]["tiles"]) == 6


def test_macro_engine_dry_runs():
    me = MacroEngine()
    vks = parse_hotkey("CTRL+SHIFT+M")
    assert len(vks) == 3

    res_seq = me.execute(
        "sequence",
        [
            {"kind": "hotkey", "value": "CTRL+S"},
            {"kind": "delay", "value": 10},
            {"kind": "text", "value": "git status"},
        ],
        dry_run=True,
    )
    assert res_seq["ok"] is True

    res_bad = me.execute("nonexistent_kind", "foo", dry_run=True)
    assert res_bad["ok"] is False
    assert res_bad["err"] is True


def test_cyd_simulator_renders_all_screens():
    cfg = ConfigStore().config
    stats = TelemetryCollector().sample()
    hist = [{"cpu": 28, "gpu": 42, "ram": 35} for _ in range(24)]
    for scr, sub in [
        ("deck", "overview"),
        ("vitals", "overview"),
        ("vitals", "cpu"),
        ("vitals", "gpu"),
        ("vitals", "ram"),
        ("media", "overview"),
        ("timer", "overview"),
        ("sys", "overview"),
    ]:
        png = render_cyd_frame(scr, cfg, stats, active_page=0, vitals_sub=sub, history=hist)
        assert png[:8] == b"\x89PNG\r\n\x1a\n"
        assert len(png) > 4000


def test_fastapi_endpoints():
    with TestClient(app) as client:
        r_idx = client.get("/")
        assert r_idx.status_code == 200
        assert "HYDRO-DECK" in r_idx.text

        r_st = client.get("/api/state")
        assert r_st.status_code == 200
        body = r_st.json()
        assert "config" in body and "stats" in body and "serial" in body

        r_png = client.get("/api/cyd/preview.png?screen=vitals&vitals_sub=gpu")
        assert r_png.status_code == 200
        assert r_png.headers["content-type"] == "image/png"

        r_test = client.post(
            "/api/macro/test",
            json={"action_type": "hotkey", "action_payload": "CTRL+SHIFT+S", "dry_run": True},
        )
        assert r_test.status_code == 200
        assert r_test.json()["ok"] is True

        r_sd = client.post("/api/firmware/export_sd")
        assert r_sd.status_code == 200
        assert r_sd.json()["ok"] is True
        assert len(r_sd.json()["files"]) >= 1

