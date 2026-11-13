from __future__ import annotations

import asyncio
import os
import queue
import shutil
import threading
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Response, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .audio_win32 import list_audio_devices, set_master_volume, toggle_mic_mute
from .config_store import ConfigStore
from .cyd_simulator import render_cyd_frame
from .macro_engine import MacroEngine
from .serial_bridge import SerialBridge, list_serial_ports
from .telemetry import TelemetryCollector
from .virtual_cyd import VirtualCydBridge
from .cydemu_bridge import CydEmuBridge

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
WEB_DIR = Path(__file__).resolve().parent.parent / "web"
FW_BUILD_DIR = ROOT_DIR / "firmware" / ".pio" / "build"
FW_DIST_DIR = ROOT_DIR / "firmware_dist"

store = ConfigStore()
telemetry = TelemetryCollector()
macros = MacroEngine()
_state_lock = threading.RLock()
_action_queue: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=128)

state_runtime: dict[str, Any] = {
    "active_screen": "deck",
    "active_page": 0,
    "vitals_sub": "overview",
    "pressed_tile": -1,
    "toast": "",
    "toast_until": 0.0,
    "latest_stats": {},
    "history": [],
}

_ws_clients: set[WebSocket] = set()
_vcyd_ws_clients: set[WebSocket] = set()
_event_loop: asyncio.AbstractEventLoop | None = None
_worker_stop = threading.Event()
_worker_thread: threading.Thread | None = None


def _broadcast_device_packet(payload: dict[str, Any]) -> bool:
    ok_ser = serial_bridge.send_packet(payload)
    ok_v = virtual_cyd.send_packet(payload)
    ok_emu = cydemu_bridge.send_packet(payload)
    return ok_ser or ok_v or ok_emu


def _set_toast(msg: str, sec: float = 1.6) -> None:
    with _state_lock:
        state_runtime["toast"] = msg
        state_runtime["toast_until"] = time.monotonic() + sec


def _broadcast_studio_ws(payload: dict[str, Any]) -> None:
    loop = _event_loop
    if loop is None or loop.is_closed() or not _ws_clients:
        return

    async def _send_all() -> None:
        dead: list[WebSocket] = []
        for ws in list(_ws_clients):
            try:
                await ws.send_json(payload)
            except Exception:
                dead.append(ws)
        for ws in dead:
            _ws_clients.discard(ws)

    try:
        asyncio.run_coroutine_threadsafe(_send_all(), loop)
    except Exception:
        pass


def _execute_tile(
    page_idx: int,
    tile_idx: int,
    hold: bool = False,
    repeat: bool = False,
    dry_run: bool = False,
    async_webhook: bool = False,
) -> dict[str, Any]:
    with _state_lock:
        pages = store.config.get("pages", [])
        if not (0 <= page_idx < len(pages)):
            raise ValueError("Invalid page index")
        tiles = pages[page_idx].get("tiles", [])
        if not (0 <= tile_idx < len(tiles)):
            raise ValueError("Invalid tile index")
        tile = dict(tiles[tile_idx])

    if repeat:
        atype = tile.get("action_type", "")
        apayload = tile.get("action_payload", "")
    elif hold and tile.get("hold_action_type"):
        atype = tile.get("hold_action_type", "")
        apayload = tile.get("hold_action_payload", "")
    elif (not hold) and tile.get("tog") and bool(tile.get("on", False)) and tile.get("state_b_action_type"):
        atype = tile.get("state_b_action_type", "")
        apayload = tile.get("state_b_action_payload", "")
    else:
        atype = tile.get("action_type", "")
        apayload = tile.get("action_payload", "")

    if atype == "folder":
        jmp = int(tile.get("jmp", apayload if str(apayload).isdigit() else 0))
        with _state_lock:
            pages = store.config.get("pages", [])
            valid = 0 <= jmp < len(pages)
            pg_name = pages[jmp]["name"] if valid else ""
            if valid and not dry_run:
                state_runtime["active_page"] = jmp
        if valid:
            if not dry_run:
                _broadcast_device_packet({"t": "page", "p": jmp})
            return {"ok": True, "toast": pg_name, "color": "#00F0DA", "detail": f"Switched to {pg_name}"}

    if atype == "cyd_screen":
        raw_t = str(apayload or "deck").strip().lower()
        scr_name, _, sub_name = raw_t.partition(":")
        scr_map = {"deck": 0, "vitals": 1, "media": 2, "timer": 3, "sys": 4}
        vsub_map = {"overview": 0, "cpu": 1, "gpu": 2, "ram": 3}
        scr_idx = scr_map.get(scr_name, 0)
        vsub_idx = vsub_map.get(sub_name, 0)
        if not dry_run:
            with _state_lock:
                state_runtime["active_screen"] = scr_name if scr_name in scr_map else "deck"
                if sub_name in vsub_map:
                    state_runtime["vitals_sub"] = sub_name
            pkt_s: dict[str, Any] = {"t": "scr", "s": scr_idx}
            if scr_idx == 1 and sub_name in vsub_map:
                pkt_s["v"] = vsub_idx
            _broadcast_device_packet(pkt_s)
        return {"ok": True, "toast": f"SCR:{scr_name.upper()}", "color": "#00F0DA", "detail": f"Switched CYD screen -> {raw_t}"}

    if atype == "timer":
        cmd = str(apayload or "focus").strip().lower()
        presets = {"preset_5m": 300, "preset_15m": 900, "preset_25m": 1500}
        sec = 0
        if cmd in presets:
            sec = presets[cmd]
            cmd = "preset"
        elif cmd.startswith("preset:"):
            try:
                sec = max(1, min(86400, int(cmd.split(":", 1)[1])))
            except ValueError:
                raise ValueError(f"Invalid timer preset seconds: {cmd}")
            cmd = "preset"
        if cmd not in ("focus", "start", "pause", "toggle", "reset", "preset", "stopwatch"):
            raise ValueError(f"Unknown timer command: {cmd}")
        if not dry_run:
            with _state_lock:
                state_runtime["active_screen"] = "timer"
            _broadcast_device_packet({"t": "scr", "s": 3})
            if cmd != "focus":
                _broadcast_device_packet({"t": "tmr", "cmd": cmd, "sec": sec})
        return {"ok": True, "toast": f"TMR:{cmd.upper()[:10]}", "color": "#FFB62E", "detail": f"Timer -> {cmd} {sec}s".strip()}

    res = macros.execute(atype, apayload, dry_run=dry_run, async_webhook=async_webhook)

    if not dry_run and not repeat and tile.get("tog"):
        with _state_lock:
            live_tile = store.config["pages"][page_idx]["tiles"][tile_idx]
            if "toggle_state" in res:
                live_tile["on"] = bool(res["toggle_state"])
            else:
                live_tile["on"] = not bool(live_tile.get("on", False))
            on_val = live_tile["on"]
            store.save()
            has_ab = bool(live_tile.get("on_label") or live_tile.get("off_label"))
            pkt = store.to_device_deck_packet(state_runtime["active_page"]) if has_ab else None
        _broadcast_device_packet({"t": "tile_state", "p": page_idx, "i": tile_idx, "on": on_val})
        if pkt is not None:
            _broadcast_device_packet(pkt)

    toast_txt = str(res.get("toast", tile.get("label", "OK")))
    if not dry_run:
        _set_toast(toast_txt)
        _broadcast_device_packet({
            "t": "toast",
            "m": toast_txt[:24],
            "c": str(res.get("color", "#00F0DA")),
            "err": bool(res.get("err", False)),
        })
    return res


def _handle_device_event_sync(msg: dict[str, Any]) -> None:
    t = msg.get("t")
    if t == "ready":
        with _state_lock:
            pkt = store.to_device_deck_packet(state_runtime["active_page"])
            cfg = {"t": "cfg", **store.config.get("device_settings", {})}
        _broadcast_device_packet(pkt)
        _broadcast_device_packet(cfg)
    elif t == "tap":
        p = int(msg.get("p", 0))
        i = int(msg.get("i", 0))
        try:
            _execute_tile(p, i, hold=False, async_webhook=True)
        except Exception:
            pass
    elif t == "hold":
        p = int(msg.get("p", 0))
        i = int(msg.get("i", 0))
        try:
            _execute_tile(p, i, hold=True, async_webhook=True)
        except Exception:
            pass
    elif t == "rpt":
        p = int(msg.get("p", 0))
        i = int(msg.get("i", 0))
        try:
            _execute_tile(p, i, hold=False, repeat=True, async_webhook=True)
        except Exception:
            pass
    elif t == "page":
        with _state_lock:
            state_runtime["active_page"] = int(msg.get("p", 0))
    elif t == "scr":
        scr_map = {0: "deck", 1: "vitals", 2: "media", 3: "timer", 4: "sys"}
        vsub_map = {0: "overview", 1: "cpu", 2: "gpu", 3: "ram"}
        with _state_lock:
            state_runtime["active_screen"] = scr_map.get(int(msg.get("p", 0)), "deck")
            if "i" in msg and int(msg.get("i", -1)) in vsub_map:
                state_runtime["vitals_sub"] = vsub_map[int(msg["i"])]
    elif t == "cfg":
        with _state_lock:
            ds = store.config.setdefault("device_settings", {})
            for k in ("bl", "rot", "inv", "bgr", "led", "sfx", "slp"):
                if k in msg:
                    ds[k] = msg[k]
            store.save()
            ds_copy = dict(ds)
        _broadcast_studio_ws({"type": "cfg_update", "device_settings": ds_copy})
    elif t == "media":
        macros.execute("media", str(msg.get("v", "play")))
    elif t == "vol":
        set_master_volume(int(msg.get("p", 50)))
    elif t == "mic":
        m = toggle_mic_mute()
        _set_toast("MIC MUTED" if m else "MIC LIVE")
        _broadcast_device_packet({"t": "stat", "mmute": m})


def _action_worker_loop() -> None:
    while not _worker_stop.is_set():
        try:
            msg = _action_queue.get(timeout=0.25)
        except queue.Empty:
            continue
        try:
            _handle_device_event_sync(msg)
        except Exception:
            pass
        finally:
            _action_queue.task_done()


def _enqueue_device_event(msg: dict[str, Any]) -> None:
    if msg.get("t") == "ready":
        _handle_device_event_sync(msg)
        return
    try:
        _action_queue.put_nowait(msg)
    except queue.Full:
        try:
            _action_queue.get_nowait()
        except queue.Empty:
            pass
        try:
            _action_queue.put_nowait(msg)
        except queue.Full:
            pass


serial_bridge = SerialBridge(on_event=_enqueue_device_event)
virtual_cyd = VirtualCydBridge(on_event=_enqueue_device_event)
cydemu_bridge = CydEmuBridge(on_event=_enqueue_device_event)   # firmware .bin running in cyd-emu


def _sync_dynamic_toggles(stats: dict[str, Any]) -> None:
    for p in store.config.get("pages", []):
        for tile in p.get("tiles", []):
            if not tile.get("tog"):
                continue
            pl = str(tile.get("action_payload", "")).lower()
            if tile.get("action_type") != "media" and tile.get("state_b_action_type") == "media":
                pl = str(tile.get("state_b_action_payload", "")).lower()
            if tile.get("action_type") == "media" or tile.get("state_b_action_type") == "media":
                if pl == "mute_mic":
                    tile["on"] = bool(stats.get("mmute", False))
                elif pl in ("play_pause", "play"):
                    tile["on"] = bool(stats.get("mplay", False))
                elif pl in ("mute_vol", "mute"):
                    tile["on"] = bool(stats.get("vmute", False))


async def _telemetry_loop() -> None:
    last_app_sig = ("", "")
    while True:
        try:
            stats = await asyncio.to_thread(telemetry.sample)
            with _state_lock:
                state_runtime["latest_stats"] = stats
                _sync_dynamic_toggles(stats)

                hist = state_runtime["history"]
                hist.append({
                    "ts": stats["clk"],
                    "cpu": stats["cpu"],
                    "gpu": stats["gpu"],
                    "ram": stats["ram"],
                    "ndn": stats["ndn"],
                    "nup": stats["nup"],
                })
                if len(hist) > 60:
                    del hist[:-60]

                if state_runtime["toast"] and time.monotonic() > state_runtime["toast_until"]:
                    state_runtime["toast"] = ""

                # Per-app + window-title auto-switch & optional return home
                auto_pkt = None
                if store.config.get("auto_profile_switch"):
                    exe = str(stats.get("active_exe", "")).lower()
                    title = str(stats.get("active_title", "")).lower()
                    sig = (exe, title)
                    if sig != last_app_sig and (exe or title):
                        last_app_sig = sig
                        matched_pg: int | None = None
                        for rule in store.config.get("app_rules", []):
                            if not rule.get("enabled"):
                                continue
                            proc = str(rule.get("process", "")).strip().lower()
                            tsub = str(rule.get("title_contains", "")).strip().lower()
                            if not proc and not tsub:
                                continue
                            proc_ok = (not proc) or (proc == exe) or (proc in exe)
                            title_ok = (not tsub) or (tsub in title)
                            if proc_ok and title_ok:
                                cand = int(rule.get("page_index", 0))
                                if 0 <= cand < len(store.config.get("pages", [])):
                                    matched_pg = cand
                                break
                        if matched_pg is None and store.config.get("auto_return_home") and state_runtime["active_page"] != 0:
                            matched_pg = 0
                        if matched_pg is not None and state_runtime["active_page"] != matched_pg:
                            state_runtime["active_page"] = matched_pg
                            auto_pkt = {"t": "page", "p": matched_pg, "keep": True}
                            _set_toast(f"AUTO: {store.config['pages'][matched_pg]['name']}")
                hist_slice = list(hist[-48:])
                rt_snap = {
                    "active_screen": state_runtime["active_screen"],
                    "active_page": state_runtime["active_page"],
                    "vitals_sub": state_runtime["vitals_sub"],
                    "toast": state_runtime["toast"],
                }

            if auto_pkt is not None:
                _broadcast_device_packet(auto_pkt)

            serial_pkt = telemetry.to_device_packet(stats)
            serial_bridge.send_packet(serial_pkt)
            virtual_cyd.send_packet(serial_pkt)
            cydemu_bridge.send_packet(serial_pkt)

            if _ws_clients:
                payload = {
                    "type": "tick",
                    "stats": stats,
                    "history": hist_slice,
                    "serial": serial_bridge.diagnostics(),
                    "virtual_cyd": virtual_cyd.get_state(),
                    "cydemu": cydemu_bridge.diagnostics(),
                    "runtime": rt_snap,
                    "macro_history": macros.recent_history(12),
                }
                dead: list[WebSocket] = []
                for ws in list(_ws_clients):
                    try:
                        await ws.send_json(payload)
                    except Exception:
                        dead.append(ws)
                for ws in dead:
                    _ws_clients.discard(ws)
        except Exception:
            pass
        await asyncio.sleep(0.85)


async def _vcyd_stream_loop() -> None:
    last_seq = -1
    last_hud_ts = 0.0
    while True:
        try:
            if _vcyd_ws_clients and virtual_cyd.initialized:
                st = virtual_cyd.get_state()
                seq = int(st.get("frame_seq", 0))
                now = time.monotonic()
                if seq != last_seq:
                    last_seq = seq
                    rgba = virtual_cyd.get_rgba_bytes()
                    for ws in list(_vcyd_ws_clients):
                        try:
                            await ws.send_bytes(rgba)
                        except Exception:
                            _vcyd_ws_clients.discard(ws)
                audio = virtual_cyd.pop_audio_events()
                if audio:
                    for ws in list(_vcyd_ws_clients):
                        try:
                            await ws.send_json({"type": "audio", "events": audio})
                        except Exception:
                            _vcyd_ws_clients.discard(ws)
                if now - last_hud_ts > 0.25:
                    last_hud_ts = now
                    for ws in list(_vcyd_ws_clients):
                        try:
                            await ws.send_json({"type": "state", "state": st})
                        except Exception:
                            _vcyd_ws_clients.discard(ws)
        except Exception:
            pass
        await asyncio.sleep(0.033)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    global _event_loop, _worker_thread
    _event_loop = asyncio.get_running_loop()
    state_runtime["latest_stats"] = telemetry.sample()
    _worker_stop.clear()
    _worker_thread = threading.Thread(target=_action_worker_loop, name="hydro_actions", daemon=True)
    _worker_thread.start()

    virtual_cyd.start(panel_type=store.config.get("virtual_panel", "ili9341"))
    # Initial handshake sync into Virtual CYD once DLL boots
    with _state_lock:
        pkt = store.to_device_deck_packet(state_runtime["active_page"])
        cfg = {"t": "cfg", **store.config.get("device_settings", {})}
    virtual_cyd.send_packet(pkt)
    virtual_cyd.send_packet(cfg)
    virtual_cyd.send_packet(telemetry.to_device_packet(state_runtime["latest_stats"]))

    if store.config.get("auto_connect", True) and os.environ.get("HYDRO_SKIP_SERIAL") != "1":
        serial_bridge.start(
            store.config.get("serial_port", "COM11"),
            int(store.config.get("baud_rate", 115200)),
        )
    if os.environ.get("HYDRO_SKIP_CYDEMU") != "1":
        cydemu_bridge.start()
    telem_task = asyncio.create_task(_telemetry_loop())
    vcyd_task = asyncio.create_task(_vcyd_stream_loop())
    try:
        yield
    finally:
        telem_task.cancel()
        vcyd_task.cancel()
        _worker_stop.set()
        serial_bridge.stop()
        cydemu_bridge.stop()
        virtual_cyd.stop()
        telemetry.close()


app = FastAPI(title="HYDRO-DECK Studio API", version="1.1.0", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=str(WEB_DIR)), name="static")


@app.get("/")
def serve_index() -> FileResponse:
    return FileResponse(str(WEB_DIR / "index.html"))


@app.get("/virtual-cyd")
def serve_virtual_cyd() -> FileResponse:
    return FileResponse(str(WEB_DIR / "virtual_cyd.html"))


@app.get("/api/state")
def get_state() -> dict[str, Any]:
    return {
        "config": store.config,
        "stats": state_runtime["latest_stats"] or telemetry.sample(),
        "history": state_runtime["history"][-48:],
        "serial": serial_bridge.diagnostics(),
        "ports": list_serial_ports(),
        "runtime": {
            "active_screen": state_runtime["active_screen"],
            "active_page": state_runtime["active_page"],
            "vitals_sub": state_runtime["vitals_sub"],
            "toast": state_runtime["toast"],
        },
        "macro_history": macros.recent_history(15),
        "firmware_bins": _available_bins(),
        "virtual_cyd": virtual_cyd.get_state(),
        "cydemu": cydemu_bridge.diagnostics(),
    }


def _available_bins() -> dict[str, Any]:
    out = {}
    for env in ("e32r28t_ili9341", "e32r28t_st7789"):
        bin_p = FW_BUILD_DIR / env / "firmware.bin"
        out[env] = {
            "exists": bin_p.exists(),
            "path": str(bin_p),
            "size_kb": round(bin_p.stat().st_size / 1024, 1) if bin_p.exists() else 0,
        }
    return out


def _validate_config_patch(cfg: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(cfg, dict):
        raise HTTPException(status_code=422, detail="Config payload must be a JSON object")
    clean = dict(cfg)
    if "pages" in clean:
        pages = clean["pages"]
        if not isinstance(pages, list) or not (1 <= len(pages) <= 6):
            raise HTTPException(status_code=422, detail="'pages' must contain 1..6 deck pages")
        for idx, pg in enumerate(pages):
            if not isinstance(pg, dict) or not isinstance(pg.get("tiles"), list):
                raise HTTPException(status_code=422, detail=f"Page {idx} must include a 'tiles' array")
            pg["tiles"] = pg["tiles"][:6]
    if "device_settings" in clean:
        ds = clean["device_settings"]
        if not isinstance(ds, dict):
            raise HTTPException(status_code=422, detail="'device_settings' must be an object")
        if "bl" in ds:
            ds["bl"] = max(25, min(255, int(ds["bl"])))
        if "rot" in ds:
            ds["rot"] = 3 if int(ds["rot"]) == 3 else 1
        if "led" in ds:
            ds["led"] = max(0, min(3, int(ds["led"])))
        if "slp" in ds:
            ds["slp"] = max(0, min(3600, int(ds["slp"])))
    if "app_rules" in clean and not isinstance(clean["app_rules"], list):
        raise HTTPException(status_code=422, detail="'app_rules' must be a list")
    return clean


@app.post("/api/config")
def save_config(cfg: dict[str, Any]) -> dict[str, Any]:
    valid_cfg = _validate_config_patch(cfg)
    with _state_lock:
        store.config.update(valid_cfg)
        n_pages = len(store.config.get("pages", [])) or 1
        state_runtime["active_page"] = max(0, min(n_pages - 1, int(state_runtime["active_page"])))
        store.save()
        pkt = store.to_device_deck_packet(state_runtime["active_page"])
        dcfg = {"t": "cfg", **store.config.get("device_settings", {})}
    _broadcast_device_packet(pkt)
    _broadcast_device_packet(dcfg)
    _set_toast("STUDIO SYNCED")
    return {"ok": True, "config": store.config}


@app.post("/api/config/reset")
def reset_config() -> dict[str, Any]:
    with _state_lock:
        cfg = store.reset_defaults()
        pkt = store.to_device_deck_packet(0)
        dcfg = {"t": "cfg", **cfg.get("device_settings", {})}
    _broadcast_device_packet(pkt)
    _broadcast_device_packet(dcfg)
    _set_toast("DEFAULTS LOADED")
    return {"ok": True, "config": cfg}


@app.get("/api/config/export")
def export_config() -> Response:
    return Response(
        content=store.export_json(),
        media_type="application/json",
        headers={"Content-Disposition": "attachment; filename=hydro_deck_profile.json"},
    )


class ImportPayload(BaseModel):
    raw_json: str


@app.post("/api/config/import")
def import_config(req: ImportPayload) -> dict[str, Any]:
    try:
        with _state_lock:
            cfg = store.import_json(req.raw_json)
            pkt = store.to_device_deck_packet(0)
        _broadcast_device_packet(pkt)
        return {"ok": True, "config": cfg}
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc))


class MacroTestRequest(BaseModel):
    page: int | None = None
    tile: int | None = None
    hold: bool = False
    action_type: str | None = None
    action_payload: Any = ""
    dry_run: bool = False


@app.post("/api/macro/test")
def test_macro(req: MacroTestRequest) -> dict[str, Any]:
    if req.page is not None and req.tile is not None:
        return _execute_tile(req.page, req.tile, hold=req.hold, dry_run=req.dry_run)
    if req.action_type:
        res = macros.execute(req.action_type, req.action_payload, dry_run=req.dry_run)
        if not req.dry_run:
            _set_toast(str(res.get("toast", "OK")))
        return res
    raise HTTPException(status_code=400, detail="Provide page+tile or action_type")


class TouchSimRequest(BaseModel):
    screen: str = "deck"
    page: int = 0
    vitals_sub: str = "overview"
    tile: int | None = None


@app.post("/api/cyd/touch_sim")
def cyd_touch_sim(req: TouchSimRequest) -> dict[str, Any]:
    with _state_lock:
        state_runtime["active_screen"] = req.screen
        state_runtime["active_page"] = req.page
        state_runtime["vitals_sub"] = req.vitals_sub
    scr_idx = {"deck": 0, "vitals": 1, "media": 2, "timer": 3, "sys": 4}.get(req.screen, 0)
    vsub_idx = {"overview": 0, "cpu": 1, "gpu": 2, "ram": 3}.get(req.vitals_sub, 0)
    _broadcast_device_packet({"t": "page", "p": int(req.page), "keep": True})
    pkt: dict[str, Any] = {"t": "scr", "s": scr_idx}
    if req.screen == "vitals":
        pkt["v"] = vsub_idx
    _broadcast_device_packet(pkt)
    if req.screen == "deck" and req.tile is not None:
        res = _execute_tile(req.page, req.tile, hold=False)
        return {"ok": True, "result": res, "runtime": state_runtime}
    return {"ok": True, "runtime": state_runtime}


class VolumeRequest(BaseModel):
    pct: int


@app.post("/api/volume")
def api_set_volume(req: VolumeRequest) -> dict[str, Any]:
    nv = set_master_volume(max(0, min(100, req.pct)))
    _broadcast_device_packet({"t": "stat", "vol": nv})
    return {"ok": True, "vol": nv}


@app.post("/api/mic/toggle")
def api_toggle_mic() -> dict[str, Any]:
    m = toggle_mic_mute()
    with _state_lock:
        _sync_dynamic_toggles({"mmute": m, **{k: v for k, v in (state_runtime["latest_stats"] or {}).items() if k in ("mplay", "vmute")}})
    _broadcast_device_packet({"t": "stat", "mmute": m})
    _set_toast("MIC MUTED" if m else "MIC LIVE")
    return {"ok": True, "mic_muted": m}


@app.get("/api/audio/devices")
def api_audio_devices() -> dict[str, Any]:
    try:
        return {"ok": True, "devices": list_audio_devices()}
    except Exception as exc:
        return {"ok": False, "devices": [], "error": str(exc)}


class WifiConfigRequest(BaseModel):
    ssid: str = Field(min_length=1, max_length=63)
    password: str = Field(default="", max_length=63)
    token: str = Field(default="", max_length=31)


@app.post("/api/wifi_config")
def api_wifi_config(req: WifiConfigRequest) -> dict[str, Any]:
    import json as _json

    FW_DIST_DIR.mkdir(parents=True, exist_ok=True)
    out = FW_DIST_DIR / "wifi.json"
    out.write_text(_json.dumps({"ssid": req.ssid, "pass": req.password, "token": req.token}, indent=2), encoding="utf-8")
    return {"ok": True, "path": str(out), "file": out.name}


@app.post("/api/media/{cmd}")
def api_media_cmd(cmd: str) -> dict[str, Any]:
    return macros.execute("media", cmd)


class SerialConnectRequest(BaseModel):
    port: str
    baud: int = 115200


@app.post("/api/serial/connect")
def api_serial_connect(req: SerialConnectRequest) -> dict[str, Any]:
    with _state_lock:
        store.config["serial_port"] = req.port
        store.config["baud_rate"] = req.baud
        store.save()
    serial_bridge.configure(req.port, req.baud)
    return {"ok": True, "serial": serial_bridge.diagnostics()}


@app.post("/api/serial/sync")
def api_serial_sync() -> dict[str, Any]:
    with _state_lock:
        pkt = store.to_device_deck_packet(state_runtime["active_page"])
        dcfg = {"t": "cfg", **store.config.get("device_settings", {})}
    ok1 = _broadcast_device_packet(pkt)
    ok2 = _broadcast_device_packet(dcfg)
    _set_toast("PUSHED TO CYD" if ok1 else "PORT WAITING")
    return {"ok": ok1 and ok2, "serial": serial_bridge.diagnostics(), "virtual_cyd": virtual_cyd.get_state()}


@app.post("/api/serial/disconnect")
def api_serial_disconnect() -> dict[str, Any]:
    serial_bridge.disconnect()
    _set_toast("SERIAL RELEASED")
    return {"ok": True, "serial": serial_bridge.diagnostics()}


@app.post("/api/cyd/calibrate")
def api_cyd_calibrate() -> dict[str, Any]:
    ok = _broadcast_device_packet({"t": "cal"})
    _set_toast("CALIBRATION SENT")
    return {"ok": ok}


@app.post("/api/cyd/sleep")
def api_cyd_sleep() -> dict[str, Any]:
    ok = _broadcast_device_packet({"t": "sleep"})
    _set_toast("CYD SLEEP SENT")
    return {"ok": ok}


@app.get("/api/templates")
def api_list_templates() -> dict[str, Any]:
    return {"ok": True, "templates": store.list_templates()}


class TemplateApplyRequest(BaseModel):
    template_id: str
    mode: str = "append"


@app.post("/api/templates/apply")
def api_apply_template(req: TemplateApplyRequest) -> dict[str, Any]:
    try:
        with _state_lock:
            cfg = store.apply_template(req.template_id, mode=req.mode)
            new_idx = max(0, len(cfg["pages"]) - 1) if req.mode == "append" else 0
            state_runtime["active_page"] = new_idx
            pkt = store.to_device_deck_packet(new_idx)
        _broadcast_device_packet(pkt)
        _set_toast("TEMPLATE ADDED")
        return {"ok": True, "config": cfg, "active_page": new_idx}
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@app.post("/api/pages/{idx}/delete")
def api_delete_page(idx: int) -> dict[str, Any]:
    try:
        with _state_lock:
            cfg = store.delete_page(idx)
            cur = int(state_runtime.get("active_page", 0))
            if cur >= len(cfg["pages"]):
                cur = max(0, len(cfg["pages"]) - 1)
            elif cur > idx:
                cur -= 1
            state_runtime["active_page"] = cur
            pkt = store.to_device_deck_packet(cur)
        _broadcast_device_packet(pkt)
        _set_toast("PAGE DELETED")
        return {"ok": True, "config": cfg, "active_page": cur}
    except Exception as exc:
        raise HTTPException(status_code=400, detail=str(exc))


FLASH_GUIDE = '''HYDRO-DECK FLASH GUIDE (2.8" CYD: E32R28T / ESP32-2432S028R)
=============================================================
Images in this folder:
  HYDRO-DECK_E32R28T_ILI9341_firmware.bin   ILI9341 panels (most boards)
  HYDRO-DECK_E32R28T_ST7789_firmware.bin    ST7789 panels
  HYDRO-DECK_E32R28T_<panel>_MERGED.bin     single file for web flashers

1. USB with esptool or PlatformIO (four files):
     0x1000   <variant>_bootloader.bin
     0x8000   <variant>_partitions.bin
     0xe000   boot_app0.bin
     0x10000  <variant>_firmware.bin
   In PC Studio click 'Release COM Port' first so the port is free.

2. Web flasher (https://esp.huhn.me or similar):
   flash <variant>_MERGED.bin at address 0x0.

3. If esptool reports 'no sync reply' or 'No serial data received':
   - Some boards only enter download mode by hand: hold BOOT while plugging USB in,
     then flash straight away.
   - Unplug anything on the rear 4-pin UART connector (P2). Its RX pin is GPIO3 (RXD0),
     the same net as the CH340's TX (through a 100 ohm resistor), so a GPS or other
     module there can block PC -> board data.

4. Do NOT install this through another firmware's microSD "update firmware" screen
   (HaleHound / WontHound and similar). The partition table has a single app slot, so
   such an updater would have to overwrite the firmware it is running from. ESP-IDF
   refuses that write and the board just reboots; if it did not refuse, an interrupted
   write would leave the board unbootable.
'''


@app.post("/api/firmware/export_sd")
def export_firmware_sd() -> dict[str, Any]:
    FW_DIST_DIR.mkdir(parents=True, exist_ok=True)
    exported: list[str] = []
    boot_app0_candidates = [
        Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin",
    ]
    for env, prefix in (
        ("e32r28t_ili9341", "HYDRO-DECK_E32R28T_ILI9341"),
        ("e32r28t_st7789", "HYDRO-DECK_E32R28T_ST7789"),
    ):
        src_dir = FW_BUILD_DIR / env
        src_fw = src_dir / "firmware.bin"
        if src_fw.exists():
            dst_fw = FW_DIST_DIR / f"{prefix}_firmware.bin"
            shutil.copy2(src_fw, dst_fw)
            exported.append(dst_fw.name)
        for part in ("bootloader.bin", "partitions.bin"):
            src_p = src_dir / part
            if src_p.exists():
                dst_p = FW_DIST_DIR / f"{prefix}_{part}"
                shutil.copy2(src_p, dst_p)
                exported.append(dst_p.name)
    for ba in boot_app0_candidates:
        if ba.exists():
            dst_ba = FW_DIST_DIR / "boot_app0.bin"
            shutil.copy2(ba, dst_ba)
            exported.append(dst_ba.name)
            break

    readme = FW_DIST_DIR / "SD_CARD_FLASH_README.txt"
    readme.write_text(FLASH_GUIDE, encoding="utf-8")
    exported.append(readme.name)
    return {"ok": len(exported) > 1, "files": exported, "dir": str(FW_DIST_DIR)}


@app.get("/api/firmware/download/{filename}")
def download_firmware_file(filename: str) -> FileResponse:
    safe_name = Path(filename).name
    if not safe_name or safe_name != filename:
        raise HTTPException(status_code=400, detail="Invalid filename")
    target = FW_DIST_DIR / safe_name
    if not target.exists():
        # Auto-export once if requested before export_sd
        export_firmware_sd()
    if not target.exists():
        raise HTTPException(status_code=404, detail=f"Firmware artifact not found: {safe_name}")
    return FileResponse(str(target), filename=safe_name, media_type="application/octet-stream")


@app.get("/api/cyd/preview.png")
def cyd_preview_png(
    screen: str | None = None,
    page: int | None = None,
    vitals_sub: str | None = None,
    scale: int = 1,
    mock: bool = False,
) -> Response:
    if not mock and virtual_cyd.initialized:
        png = virtual_cyd.render_png_bytes(scale=max(1, min(4, int(scale))))
        return Response(content=png, media_type="image/png", headers={"Cache-Control": "no-store"})
    scr = screen or state_runtime["active_screen"]
    pg = page if page is not None else state_runtime["active_page"]
    vsub = vitals_sub or state_runtime["vitals_sub"]
    png = render_cyd_frame(
        screen=scr,
        config=store.config,
        stats=state_runtime["latest_stats"] or telemetry.sample(),
        active_page=pg,
        vitals_sub=vsub,
        history=state_runtime["history"],
        connected=serial_bridge.hydro_handshake,
        pressed_tile=state_runtime["pressed_tile"],
        toast=state_runtime["toast"],
    )
    return Response(content=png, media_type="image/png", headers={"Cache-Control": "no-store"})


class VirtualTouchReq(BaseModel):
    down: bool
    x: int = Field(default=0, ge=0, le=319)
    y: int = Field(default=0, ge=0, le=239)


class VirtualBootReq(BaseModel):
    down: bool


class VirtualPanelReq(BaseModel):
    panel: str = "ili9341"


@app.get("/api/virtual/state")
def virtual_state() -> dict[str, Any]:
    return {"ok": virtual_cyd.initialized, "state": virtual_cyd.get_state()}


@app.get("/api/virtual/frame.png")
def virtual_frame_png(scale: int = 1) -> Response:
    if not virtual_cyd.initialized:
        raise HTTPException(status_code=503, detail=virtual_cyd.last_error or "Virtual CYD not initialized")
    png = virtual_cyd.render_png_bytes(scale=max(1, min(4, int(scale))))
    return Response(content=png, media_type="image/png", headers={"Cache-Control": "no-store"})


@app.post("/api/virtual/touch")
def virtual_touch(req: VirtualTouchReq) -> dict[str, Any]:
    virtual_cyd.set_touch(req.down, req.x, req.y)
    return {"ok": True}


@app.post("/api/virtual/boot")
def virtual_boot(req: VirtualBootReq) -> dict[str, Any]:
    virtual_cyd.set_boot(req.down)
    return {"ok": True}


@app.post("/api/virtual/panel")
def virtual_panel(req: VirtualPanelReq) -> dict[str, Any]:
    virtual_cyd.set_panel(req.panel)
    with _state_lock:
        store.config["virtual_panel"] = virtual_cyd.panel_type
        store.save()
    return {"ok": True, "panel": virtual_cyd.panel_type}


@app.post("/api/virtual/reset")
def virtual_reset(clear_nvs: bool = False) -> dict[str, Any]:
    ok = virtual_cyd.reset(clear_nvs=clear_nvs)
    with _state_lock:
        pkt = store.to_device_deck_packet(state_runtime["active_page"])
        cfg = {"t": "cfg", **store.config.get("device_settings", {})}
        stats = state_runtime["latest_stats"] or telemetry.sample()
    virtual_cyd.send_packet(pkt)
    virtual_cyd.send_packet(cfg)
    virtual_cyd.send_packet(telemetry.to_device_packet(stats))
    return {"ok": ok, "state": virtual_cyd.get_state()}


@app.post("/api/virtual/serial")
def virtual_serial_cmd(pkt: dict[str, Any]) -> dict[str, Any]:
    ok = virtual_cyd.send_packet(pkt)
    return {"ok": ok}


@app.post("/api/device/sync")
def api_device_sync() -> dict[str, Any]:
    with _state_lock:
        pkt = store.to_device_deck_packet(state_runtime["active_page"])
        cfg = {"t": "cfg", **store.config.get("device_settings", {})}
        stats = state_runtime["latest_stats"] or telemetry.sample()
    ok = _broadcast_device_packet(pkt)
    _broadcast_device_packet(cfg)
    _broadcast_device_packet(telemetry.to_device_packet(stats))
    _set_toast("DEVICE SYNCED")
    return {"ok": ok, "virtual_cyd": virtual_cyd.get_state(), "serial": serial_bridge.diagnostics()}


@app.websocket("/ws/virtual-cyd")
async def vcyd_ws_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    _vcyd_ws_clients.add(websocket)
    try:
        if virtual_cyd.initialized:
            await websocket.send_bytes(virtual_cyd.get_rgba_bytes())
            await websocket.send_json({"type": "state", "state": virtual_cyd.get_state()})
        while True:
            raw = await websocket.receive_json()
            mtype = raw.get("type")
            if mtype == "touch":
                virtual_cyd.set_touch(bool(raw.get("down", False)), int(raw.get("x", 0)), int(raw.get("y", 0)))
            elif mtype == "boot":
                virtual_cyd.set_boot(bool(raw.get("down", False)))
    except WebSocketDisconnect:
        _vcyd_ws_clients.discard(websocket)
    except Exception:
        _vcyd_ws_clients.discard(websocket)


@app.websocket("/ws")
async def ws_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    _ws_clients.add(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        _ws_clients.discard(websocket)
    except Exception:
        _ws_clients.discard(websocket)
