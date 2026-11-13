from __future__ import annotations

import os
import sys
import time
from pathlib import Path

from fastapi.testclient import TestClient

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from backend.config_store import ConfigStore  # noqa: E402
from backend.macro_engine import MacroEngine, expand_macro_template, parse_hotkey  # noqa: E402
from backend.virtual_cyd import VirtualCydBridge  # noqa: E402


def _tick_n(bridge: VirtualCydBridge, count: int = 4) -> None:
    for _ in range(count):
        bridge.tick_once()


def test_virtual_cyd_real_dll_screens_touch_sleep_and_cal(tmp_path: Path) -> None:
    events: list[dict] = []
    bridge = VirtualCydBridge(on_event=events.append, storage_dir=tmp_path / "vcyd")
    assert bridge.start(panel_type="ili9341", start_loop=False) is True
    try:
        _tick_n(bridge, 4)
        rgba = bridge.get_rgba_bytes()
        assert len(rgba) == 320 * 240 * 4
        assert any(rgba[i : i + 3] != b"\x00\x00\x00" for i in range(0, len(rgba), 64))

        bridge.send_packet({
            "t": "stat",
            "host": "TEST-RIG",
            "cpun": "Core i9",
            "gpun": "No NVIDIA GPU",
            "cpu": 44,
            "cclk": 4.9,
            "ctmp": -1,
            "gpu": 0,
            "gtmp": -1,
            "vtot": 0,
            "ram": 52,
            "ruse": 16.5,
            "rtot": 32.0,
            "cores": [20, 35, 50, 65, 80, 30, 40, 55, 10, 25, 45, 70, 85, 60, 15, 22],
            "vol": 68,
            "song": "Neon Drift",
        })
        _tick_n(bridge, 4)

        # Navigate bottom nav: VITALS (x=96, y=224)
        bridge.tap_px(96, 224)
        st = bridge.get_state()
        assert st["screen"] == "VITALS"

        # Tap CPU/GPU/RAM subtabs
        bridge.tap_px(122, 40)
        bridge.tap_px(198, 40)
        bridge.tap_px(275, 40)
        bridge.tap_px(44, 40)

        # Navigate MEDIA, TIMER, SYS
        bridge.tap_px(160, 224)
        assert bridge.get_state()["screen"] == "MEDIA"
        bridge.tap_px(224, 224)
        assert bridge.get_state()["screen"] == "TIMER"
        bridge.tap_px(288, 224)
        assert bridge.get_state()["screen"] == "SYS"

        # Test sleep command + smooth fade + touch wake
        bridge.send_packet({"t": "sleep"})
        for _ in range(25):
            bridge.tick_once()
            time.sleep(0.018)
        assert bridge.get_state()["sleeping"] is True
        assert bridge.get_state()["backlight_pwm"] == 0
        bridge.tap_px(160, 120)
        for _ in range(20):
            bridge.tick_once()
            time.sleep(0.018)
        assert bridge.get_state()["sleeping"] is False
        assert bridge.get_state()["backlight_pwm"] > 100

        # Test ST7789 panel switch
        bridge.set_panel("st7789")
        assert bridge.get_state()["panel_type"] == "st7789"
        bridge.set_panel("ili9341")
        assert bridge.get_state()["panel_type"] == "ili9341"

        # Test non-blocking interactive 4-point calibration FSM
        bridge.send_packet({"t": "cal"})
        _tick_n(bridge, 4)
        assert bridge.get_state()["calibrating"] is True
        for cx, cy in ((24, 24), (295, 24), (24, 215), (295, 215)):
            bridge.tap_px(cx, cy, hold_ms=150)
            time.sleep(0.18)
            _tick_n(bridge, 6)
        assert bridge.get_state()["calibrating"] is False
    finally:
        bridge.stop()


def test_macro_engine_new_capabilities(tmp_path: Path) -> None:
    assert parse_hotkey("CTRL+-") == [0xA2, 0xBD]
    assert parse_hotkey("CTRL++") == [0xA2, 0xBB]
    assert parse_hotkey("CTRL+=") == [0xA2, 0xBB]

    eng = MacroEngine(screenshots_dir=tmp_path / "shots")
    expanded = expand_macro_template("HI {{date}} {{time}} {{clipboard}}", clipboard_override="ABC")
    assert expanded.endswith("ABC")

    res_clip = eng.execute("clipboard", "TS={{clipboard}}", dry_run=True)
    assert res_clip["ok"] is True

    res_sfx = eng.execute("soundboard", "chime", dry_run=True)
    assert res_sfx["ok"] is True
    res_sfx_bad = eng.execute("soundboard", str(tmp_path / "missing.wav"), dry_run=True)
    assert res_sfx_bad["ok"] is False

    res_focus = eng.execute("app_focus", {"target": "Code.exe", "launch": "code"}, dry_run=True)
    assert res_focus["ok"] is True

    res_hook = eng.execute("webhook", {"method": "PUT", "url": "https://example.invalid/api", "body": {"k": 1}}, dry_run=True)
    assert res_hook["ok"] is True and res_hook["method"] == "PUT"
    res_hook_bad = eng.execute("webhook", {"method": "TRACE", "url": "https://example.invalid"}, dry_run=True)
    assert res_hook_bad["ok"] is False


def test_config_store_templates_and_delete_page(tmp_path: Path) -> None:
    cfg_path = tmp_path / "deck_config.json"
    store = ConfigStore(path=cfg_path)
    templates = store.list_templates()
    assert {t["id"] for t in templates} == {"developer", "streaming", "editing", "gaming"}

    store.apply_template("developer", mode="append")
    assert len(store.config["pages"]) == 5
    assert not cfg_path.with_suffix(".tmp").exists()

    store.config["pages"][0]["tiles"][5]["jmp"] = 2
    store.config["pages"][0]["tiles"][5]["action_type"] = "folder"
    store.config["pages"][0]["tiles"][5]["on_label"] = "OPEN"
    store.config["pages"][0]["tiles"][5]["off_label"] = "SHUT"
    store.config["pages"][0]["tiles"][5]["tog"] = True
    store.config["pages"][0]["tiles"][5]["on"] = True
    store.config["app_rules"].append({"process": "vlc.exe", "title_contains": "Movie", "page_index": 2, "enabled": True})
    store.save()

    pkt = store.to_device_deck_packet(0)
    assert pkt["pages"][0]["tiles"][0]["hh"] is True
    assert pkt["pages"][0]["tiles"][5]["sub"] == "SHUT"
    assert pkt["pages"][0]["tiles"][5]["sub2"] == "OPEN"

    store.delete_page(1)
    assert len(store.config["pages"]) == 4
    assert store.config["pages"][0]["tiles"][5]["jmp"] == 1
    assert any(r["process"] == "vlc.exe" and r["page_index"] == 1 for r in store.config["app_rules"])


def test_fastapi_routes_end_to_end(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setenv("HYDRO_SKIP_SERIAL", "1")
    monkeypatch.setenv("HYDRO_SKIP_CYDEMU", "1")
    import backend.app as app_mod

    iso_store = ConfigStore(path=tmp_path / "studio_cfg.json")
    iso_vcyd = VirtualCydBridge(on_event=app_mod._enqueue_device_event, storage_dir=tmp_path / "api_vcyd")
    monkeypatch.setattr(app_mod, "store", iso_store)
    monkeypatch.setattr(app_mod, "virtual_cyd", iso_vcyd)
    monkeypatch.setattr(app_mod, "FW_DIST_DIR", tmp_path / "fw_dist")

    with TestClient(app_mod.app) as client:
        assert client.get("/").status_code == 200
        assert client.get("/virtual-cyd").status_code == 200

        st = client.get("/api/state")
        assert st.status_code == 200
        body = st.json()
        assert body["virtual_cyd"]["initialized"] is True

        png = client.get("/api/virtual/frame.png")
        assert png.status_code == 200
        assert png.content[:4] == b"\x89PNG"

        tpls = client.get("/api/templates")
        assert tpls.status_code == 200 and len(tpls.json()["templates"]) == 4

        ap = client.post("/api/templates/apply", json={"template_id": "streaming", "mode": "append"})
        assert ap.status_code == 200
        new_pg = ap.json()["active_page"]

        dp = client.post(f"/api/pages/{new_pg}/delete")
        assert dp.status_code == 200

        assert client.post("/api/serial/disconnect").json()["serial"]["paused"] is True
        assert client.post("/api/cyd/calibrate").status_code == 200
        assert client.post("/api/cyd/sleep").status_code == 200
        assert client.post("/api/virtual/reset").status_code == 200
        time.sleep(0.12)

        exp = client.post("/api/firmware/export_sd")
        assert exp.status_code == 200 and exp.json()["ok"] is True
        dl_readme = client.get("/api/firmware/download/SD_CARD_FLASH_README.txt")
        assert dl_readme.status_code == 200 and b"GPIO3" in dl_readme.content
        dl_bin = client.get("/api/firmware/download/HYDRO-DECK_E32R28T_ILI9341_firmware.bin")
        assert dl_bin.status_code == 200 and len(dl_bin.content) > 100000

        # Screen & Vitals SubView synchronization must persist without snap-back
        for sub_name, expected_idx in (("cpu", 1), ("gpu", 2), ("ram", 3), ("overview", 0)):
            sim_v = client.post("/api/cyd/touch_sim", json={"screen": "vitals", "page": 0, "vitals_sub": sub_name})
            assert sim_v.status_code == 200
            time.sleep(0.09)
            st_v = client.get("/api/virtual/state").json()["state"]
            assert st_v["screen"] == "VITALS"
            assert st_v["vitals_sub"] == expected_idx

        for scr_name in ("MEDIA", "TIMER", "SYS"):
            sim_s = client.post("/api/cyd/touch_sim", json={"screen": scr_name.lower(), "page": 0})
            assert sim_s.status_code == 200
            time.sleep(0.09)
            assert client.get("/api/virtual/state").json()["state"]["screen"] == scr_name

        # /api/state ports list returns uniform device/description schema
        for p in client.get("/api/state").json().get("ports", []):
            assert "device" in p and "description" in p

        # /api/config validation must reject empty pages with 422
        bad_cfg = client.post("/api/config", json={"pages": []})
        assert bad_cfg.status_code == 422

        # State B toggle + hold_repeat roundtrip through ConfigStore & /api/macro/test
        cur_cfg = client.get("/api/state").json()["config"]
        cur_cfg["pages"][0]["tiles"][2]["tog"] = True
        cur_cfg["pages"][0]["tiles"][2]["on"] = False
        cur_cfg["pages"][0]["tiles"][2]["off_label"] = "OFF_ST"
        cur_cfg["pages"][0]["tiles"][2]["on_label"] = "ON_ST"
        cur_cfg["pages"][0]["tiles"][2]["action_type"] = "clipboard"
        cur_cfg["pages"][0]["tiles"][2]["action_payload"] = {"text": "STATE_A", "paste": True, "mode": "paste"}
        cur_cfg["pages"][0]["tiles"][2]["state_b_action_type"] = "soundboard"
        cur_cfg["pages"][0]["tiles"][2]["state_b_action_payload"] = "chime"
        saved = client.post("/api/config", json=cur_cfg)
        assert saved.status_code == 200
        assert saved.json()["config"]["pages"][0]["tiles"][2]["state_b_action_type"] == "soundboard"


def test_clipboard_paste_webhook_headers_and_cyd_repeat_cal_guards(tmp_path: Path) -> None:
    eng = MacroEngine(screenshots_dir=tmp_path / "shots2")

    # Clipboard paste mode + copy-only mode dry runs
    res_paste = eng.execute("clipboard", {"text": "HYDRO {{date}}", "paste": True, "mode": "paste"}, dry_run=True)
    assert res_paste["ok"] is True and res_paste["paste"] is True

    res_copy = eng.execute("clipboard", {"text": "COPY_ONLY", "paste": False, "mode": "copy"}, dry_run=True)
    assert res_copy["ok"] is True and res_copy["paste"] is False

    # Webhook custom headers validation
    res_wh = eng.execute(
        "webhook",
        {"method": "POST", "url": "https://example.invalid/hook", "body": {"v": 3}, "headers": {"X-Api-Key": "secret"}},
        dry_run=True,
    )
    assert res_wh["ok"] is True and res_wh["method"] == "POST"
    res_wh_bad = eng.execute(
        "webhook",
        {"method": "POST", "url": "ftp://unsupported.invalid/hook", "body": "{}"},
        dry_run=True,
    )
    assert res_wh_bad["ok"] is False

    # C++ Virtual CYD hold_repeat (rpt) + premature lift calibration guard
    events: list[dict] = []
    bridge = VirtualCydBridge(on_event=events.append, storage_dir=tmp_path / "vcyd_rpt")
    assert bridge.start(panel_type="ili9341", start_loop=False) is True
    try:
        _tick_n(bridge, 4)
        # Slot 3 (row 1, col 0 -> center x=58, y=155) defaults to VOL DOWN with hold_repeat=true
        bridge.set_touch(True, 58, 155)
        for _ in range(30):
            bridge.tick_once()
            time.sleep(0.032)
        bridge.set_touch(False, 58, 155)
        _tick_n(bridge, 4)
        acts = [e.get("t") for e in events]
        assert "hold" in acts and "rpt" in acts

        # Calibration premature lift (< 65ms hold) must NOT falsely advance out of calibration
        bridge.send_packet({"t": "cal"})
        _tick_n(bridge, 4)
        assert bridge.get_state()["calibrating"] is True
        bridge.set_touch(True, 24, 24)
        bridge.tick_once()
        time.sleep(0.01)
        bridge.set_touch(False, 24, 24)
        _tick_n(bridge, 4)
        assert bridge.get_state()["calibrating"] is True
    finally:
        bridge.stop()


