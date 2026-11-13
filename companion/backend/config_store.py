from __future__ import annotations

import copy
import json
import os
import threading
from pathlib import Path
from typing import Any

DATA_DIR = Path(__file__).resolve().parent.parent / "data"
CONFIG_PATH = DATA_DIR / "hydro_deck_config.json"


def default_config() -> dict[str, Any]:
    return {
        "serial_port": "COM11",
        "baud_rate": 115200,
        "auto_connect": True,
        "wifi_host": "",
        "auto_profile_switch": False,
        "auto_return_home": False,
        "device_settings": {
            "bl": 230,
            "rot": 1,
            "inv": False,
            "bgr": False,
            "led": 0,
            "sfx": True,
            "slp": 300,
        },
        "app_rules": [
            {"process": "Code.exe", "title_contains": "", "page_index": 1, "enabled": True},
            {"process": "WindowsTerminal.exe", "title_contains": "", "page_index": 1, "enabled": True},
            {"process": "obs64.exe", "title_contains": "", "page_index": 2, "enabled": True},
        ],
        "pages": [
            {
                "name": "MAIN",
                "tiles": [
                    {
                        "label": "MIC MUTE",
                        "sub": "LIVE",
                        "off_label": "LIVE",
                        "on_label": "MUTED",
                        "icon": "mic",
                        "col": "#FF3A62",
                        "tog": True,
                        "on": False,
                        "jmp": -1,
                        "action_type": "media",
                        "action_payload": "mute_mic",
                        "hold_action_type": "app",
                        "hold_action_payload": "ms-settings:sound",
                    },
                    {
                        "label": "SCREENSHOT",
                        "sub": "PNG SAVE",
                        "icon": "shot",
                        "col": "#00F0DA",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "screenshot",
                        "action_payload": "",
                        "hold_action_type": "hotkey",
                        "hold_action_payload": "WIN+SHIFT+S",
                    },
                    {
                        "label": "PLAY/PAUSE",
                        "sub": "PAUSED",
                        "off_label": "PAUSED",
                        "on_label": "PLAYING",
                        "icon": "play",
                        "col": "#22E88A",
                        "tog": True,
                        "on": False,
                        "jmp": -1,
                        "action_type": "media",
                        "action_payload": "play_pause",
                        "hold_action_type": "media",
                        "hold_action_payload": "next",
                    },
                    {
                        "label": "VOL DOWN",
                        "sub": "-6%",
                        "icon": "vol_dn",
                        "col": "#3898FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "hold_repeat": True,
                        "action_type": "media",
                        "action_payload": "vol_down",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "VOL UP",
                        "sub": "+6%",
                        "icon": "vol_up",
                        "col": "#3898FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "hold_repeat": True,
                        "action_type": "media",
                        "action_payload": "vol_up",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "DEV LAB",
                        "sub": "FOLDER",
                        "icon": "folder",
                        "col": "#A866FF",
                        "tog": False,
                        "on": False,
                        "jmp": 1,
                        "action_type": "folder",
                        "action_payload": "1",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                ],
            },
            {
                "name": "DEV LAB",
                "tiles": [
                    {
                        "label": "TERMINAL",
                        "sub": "WT / PS",
                        "icon": "terminal",
                        "col": "#00F0DA",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "app",
                        "action_payload": "wt.exe",
                        "hold_action_type": "app",
                        "hold_action_payload": "powershell.exe",
                    },
                    {
                        "label": "VS CODE",
                        "sub": "IDE",
                        "icon": "code",
                        "col": "#3898FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "app",
                        "action_payload": "code",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "GIT STATUS",
                        "sub": "MACRO",
                        "icon": "git",
                        "col": "#FFB62E",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "sequence",
                        "action_payload": [
                            {"kind": "text", "value": "git status -sb"},
                            {"kind": "hotkey", "value": "ENTER"},
                        ],
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "CLIPBOARD",
                        "sub": "WIN+V",
                        "icon": "clipboard",
                        "col": "#A866FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "hotkey",
                        "action_payload": "WIN+V",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "COPY/RUN",
                        "sub": "CTRL+S",
                        "icon": "rocket",
                        "col": "#22E88A",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "sequence",
                        "action_payload": [
                            {"kind": "hotkey", "value": "CTRL+S"},
                            {"kind": "delay", "value": 80},
                            {"kind": "hotkey", "value": "F5"},
                        ],
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "BACK MAIN",
                        "sub": "RETURN",
                        "icon": "back",
                        "col": "#8A9AB8",
                        "tog": False,
                        "on": False,
                        "jmp": 0,
                        "action_type": "folder",
                        "action_payload": "0",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                ],
            },
            {
                "name": "STREAM",
                "tiles": [
                    {
                        "label": "REC TOGGLE",
                        "sub": "WIN+ALT+R",
                        "icon": "rec",
                        "col": "#FF3A62",
                        "tog": True,
                        "on": False,
                        "jmp": -1,
                        "action_type": "hotkey",
                        "action_payload": "WIN+ALT+R",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "GAME BAR",
                        "sub": "WIN+G",
                        "icon": "cam",
                        "col": "#A866FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "hotkey",
                        "action_payload": "WIN+G",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "DISCORD",
                        "sub": "MUTE KEY",
                        "icon": "discord",
                        "col": "#3898FF",
                        "tog": True,
                        "on": False,
                        "jmp": -1,
                        "action_type": "hotkey",
                        "action_payload": "CTRL+SHIFT+M",
                        "hold_action_type": "hotkey",
                        "hold_action_payload": "CTRL+SHIFT+D",
                    },
                    {
                        "label": "CLIP 30s",
                        "sub": "WIN+ALT+G",
                        "icon": "flame",
                        "col": "#FFB62E",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "hotkey",
                        "action_payload": "WIN+ALT+G",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "SPOTIFY",
                        "sub": "LAUNCH",
                        "icon": "music",
                        "col": "#22E88A",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "url",
                        "action_payload": "https://open.spotify.com",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "BACK MAIN",
                        "sub": "RETURN",
                        "icon": "back",
                        "col": "#8A9AB8",
                        "tog": False,
                        "on": False,
                        "jmp": 0,
                        "action_type": "folder",
                        "action_payload": "0",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                ],
            },
            {
                "name": "SYSTEM",
                "tiles": [
                    {
                        "label": "BROWSER",
                        "sub": "NEW TAB",
                        "icon": "browser",
                        "col": "#00F0DA",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "url",
                        "action_payload": "https://www.google.com",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "SNAP LEFT",
                        "sub": "WIN+LEFT",
                        "icon": "window",
                        "col": "#3898FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "window",
                        "action_payload": "snap_left",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "SNAP RGHT",
                        "sub": "WIN+RGHT",
                        "icon": "window",
                        "col": "#3898FF",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "window",
                        "action_payload": "snap_right",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "TASK MGR",
                        "sub": "SYS MON",
                        "icon": "cpu",
                        "col": "#FFB62E",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "app",
                        "action_payload": "taskmgr.exe",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "EXPLORER",
                        "sub": "FILES",
                        "icon": "folder",
                        "col": "#22E88A",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "hotkey",
                        "action_payload": "WIN+E",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                    {
                        "label": "LOCK PC",
                        "sub": "WIN+L",
                        "icon": "lock",
                        "col": "#FF3A62",
                        "tog": False,
                        "on": False,
                        "jmp": -1,
                        "action_type": "window",
                        "action_payload": "lock_pc",
                        "hold_action_type": "",
                        "hold_action_payload": "",
                    },
                ],
            },
        ],
    }


STARTER_TEMPLATES: dict[str, dict[str, Any]] = {
    "developer": {
        "id": "developer",
        "title": "Developer & Terminal",
        "description": "Git sync, VS Code command palette, terminal, build, debug, and quick docs.",
        "page": {
            "name": "DEV PACK",
            "tiles": [
                {"label": "CMD PAL", "sub": "CTRL+SHIFT+P", "icon": "terminal", "col": "#00F0DA", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+SHIFT+P", "hold_action_type": "hotkey", "hold_action_payload": "CTRL+`"},
                {"label": "GIT PULL", "sub": "SHELL SYNC", "icon": "git", "col": "#22E88A", "tog": False, "on": False, "jmp": -1, "action_type": "shell", "action_payload": "git status -sb", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "FMT DOC", "sub": "ALT+SHIFT+F", "icon": "code", "col": "#3898FF", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "SHIFT+ALT+F", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "SNIPPET", "sub": "TIMESTAMP", "icon": "clipboard", "col": "#9E5CFF", "tog": False, "on": False, "jmp": -1, "action_type": "clipboard", "action_payload": "[{{date}} {{time}}] ", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "SPLIT RT", "sub": "SNAP RIGHT", "icon": "window", "col": "#FFB62E", "tog": False, "on": False, "jmp": -1, "action_type": "window", "action_payload": "snap_right", "hold_action_type": "window", "hold_action_payload": "snap_left"},
                {"label": "FOCUS IDE", "sub": "VS CODE", "icon": "rocket", "col": "#00F0DA", "tog": False, "on": False, "jmp": -1, "action_type": "app_focus", "action_payload": {"target": "Code.exe", "launch": "code"}, "hold_action_type": "", "hold_action_payload": ""},
            ],
        },
    },
    "streaming": {
        "id": "streaming",
        "title": "Streaming & OBS Live",
        "description": "Mic mute A/B toggle, scene switches, replay buffer, SFX chime, and stream timer.",
        "page": {
            "name": "STREAM",
            "tiles": [
                {"label": "MIC LIVE", "sub": "LIVE", "off_label": "LIVE", "on_label": "MUTED", "icon": "mic", "col": "#FF3A62", "tog": True, "on": False, "jmp": -1, "action_type": "media", "action_payload": "mute_mic", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "SCENE 1", "sub": "MAIN CAM", "icon": "cam", "col": "#00F0DA", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+ALT+F1", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "SCENE 2", "sub": "SCREEN", "icon": "window", "col": "#3898FF", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+ALT+F2", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "REPLAY", "sub": "SAVE CLIP", "icon": "rec", "col": "#FFB62E", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+SHIFT+S", "hold_action_type": "screenshot", "hold_action_payload": ""},
                {"label": "FANFARE", "sub": "SFX BEEP", "icon": "music", "col": "#9E5CFF", "tog": False, "on": False, "jmp": -1, "action_type": "soundboard", "action_payload": "chime", "hold_action_type": "soundboard", "hold_action_payload": "fanfare"},
                {"label": "GO LIVE", "sub": "STANDBY", "off_label": "STANDBY", "on_label": "ON AIR", "icon": "bolt", "col": "#22E88A", "tog": True, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+ALT+F9", "hold_action_type": "", "hold_action_payload": ""},
            ],
        },
    },
    "editing": {
        "id": "editing",
        "title": "Video & Audio Edit",
        "description": "Razor cut, ripple delete, undo/redo, shuttle playback, and timeline zoom controls.",
        "page": {
            "name": "EDIT PRO",
            "tiles": [
                {"label": "RAZOR CUT", "sub": "CTRL+B", "icon": "bolt", "col": "#FF3A62", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+B", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "UNDO STEP", "sub": "CTRL+Z", "icon": "prev", "col": "#FFB62E", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+Z", "hold_action_type": "hotkey", "hold_action_payload": "CTRL+Y"},
                {"label": "ZOOM IN", "sub": "CTRL++", "icon": "rocket", "col": "#00F0DA", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL++", "hold_action_type": "hotkey", "hold_action_payload": "CTRL+-"},
                {"label": "SHUTTLE", "sub": "PLAY/PAUSE", "icon": "play", "col": "#22E88A", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "SPACE", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "ZOOM OUT", "sub": "CTRL+-", "icon": "back", "col": "#3898FF", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "CTRL+-", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "MARK IN", "sub": "KEY I/O", "icon": "flame", "col": "#9E5CFF", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "I", "hold_action_type": "hotkey", "hold_action_payload": "O"},
            ],
        },
    },
    "gaming": {
        "id": "gaming",
        "title": "Gaming & Voice Comms",
        "description": "Instant Discord mute, Xbox Game Bar capture, overlay, and volume.",
        "page": {
            "name": "GAMING",
            "tiles": [
                {"label": "MIC MUTE", "sub": "LIVE", "off_label": "LIVE", "on_label": "MUTED", "icon": "mic", "col": "#FF3A62", "tog": True, "on": False, "jmp": -1, "action_type": "media", "action_payload": "mute_mic", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "GAME BAR", "sub": "WIN+G", "icon": "flame", "col": "#FFB62E", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "WIN+G", "hold_action_type": "", "hold_action_payload": ""},
                {"label": "REC CLIP", "sub": "WIN+ALT+G", "icon": "rec", "col": "#22E88A", "tog": False, "on": False, "jmp": -1, "action_type": "hotkey", "action_payload": "WIN+ALT+G", "hold_action_type": "screenshot", "hold_action_payload": ""},
                {"label": "VOL DOWN", "sub": "-6%", "icon": "vol_dn", "col": "#3898FF", "tog": False, "on": False, "jmp": -1, "action_type": "media", "action_payload": "vol_down", "hold_action_type": "", "hold_action_payload": "", "hold_repeat": True},
                {"label": "VOL UP", "sub": "+6%", "icon": "vol_up", "col": "#00F0DA", "tog": False, "on": False, "jmp": -1, "action_type": "media", "action_payload": "vol_up", "hold_action_type": "", "hold_action_payload": "", "hold_repeat": True},
                {"label": "SCREENSHOT", "sub": "PNG ALL", "icon": "shot", "col": "#9E5CFF", "tog": False, "on": False, "jmp": -1, "action_type": "screenshot", "action_payload": "", "hold_action_type": "", "hold_action_payload": ""},
            ],
        },
    },
}


class ConfigStore:
    def __init__(self, path: Path | None = None):
        self.path = path or CONFIG_PATH
        self._lock = threading.RLock()
        self.config: dict[str, Any] = default_config()
        self.load()

    def load(self) -> dict[str, Any]:
        with self._lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            if self.path.exists():
                try:
                    loaded = json.loads(self.path.read_text(encoding="utf-8"))
                    merged = default_config()
                    merged.update({k: v for k, v in loaded.items() if k in merged})
                    # Backfill clean off/on sub-labels on legacy dynamic toggle tiles
                    for pg in merged.get("pages", []):
                        for t in pg.get("tiles", []):
                            if t.get("tog") and not t.get("off_label") and not t.get("on_label"):
                                pl = str(t.get("action_payload", "")).lower()
                                if t.get("action_type") == "media" and pl == "mute_mic":
                                    t["off_label"] = "LIVE"
                                    t["on_label"] = "MUTED"
                                elif t.get("action_type") == "media" and pl in ("play_pause", "play"):
                                    t["off_label"] = "PAUSED"
                                    t["on_label"] = "PLAYING"
                    self.config = merged
                except Exception:
                    self.config = default_config()
            else:
                self.save()
            return self.config

    def save(self) -> None:
        with self._lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self.path.with_suffix(".tmp")
            tmp.write_text(json.dumps(self.config, indent=2), encoding="utf-8")
            os.replace(tmp, self.path)

    def reset_defaults(self) -> dict[str, Any]:
        with self._lock:
            self.config = default_config()
            self.save()
            return self.config

    def list_templates(self) -> list[dict[str, Any]]:
        return [
            {
                "id": k,
                "title": v["title"],
                "description": v["description"],
                "page_name": v["page"]["name"],
                "tiles": len(v["page"]["tiles"]),
            }
            for k, v in STARTER_TEMPLATES.items()
        ]

    def apply_template(self, template_id: str, mode: str = "append") -> dict[str, Any]:
        if template_id not in STARTER_TEMPLATES:
            raise ValueError(f"Unknown template: {template_id}")
        tpl_page = copy.deepcopy(STARTER_TEMPLATES[template_id]["page"])
        with self._lock:
            pages = self.config.setdefault("pages", [])
            if mode == "replace":
                self.config["pages"] = [tpl_page]
            else:
                if len(pages) >= 6:
                    raise ValueError("Maximum 6 pages reached")
                pages.append(tpl_page)
            self.save()
            return copy.deepcopy(self.config)

    def delete_page(self, idx: int) -> dict[str, Any]:
        with self._lock:
            pages = self.config.get("pages", [])
            if len(pages) <= 1:
                raise ValueError("Cannot delete the last remaining page")
            if not (0 <= idx < len(pages)):
                raise ValueError(f"Invalid page index: {idx}")
            pages.pop(idx)

            for pg in pages:
                for t in pg.get("tiles", []):
                    jmp = int(t.get("jmp", -1))
                    if jmp == idx:
                        t["jmp"] = -1
                        if t.get("action_type") == "folder":
                            t["action_type"] = ""
                            t["action_payload"] = ""
                    elif jmp > idx:
                        t["jmp"] = jmp - 1

            new_rules = []
            for r in self.config.get("app_rules", []):
                rp = int(r.get("page_index", 0))
                if rp == idx:
                    continue
                nr = dict(r)
                if rp > idx:
                    nr["page_index"] = rp - 1
                new_rules.append(nr)
            self.config["app_rules"] = new_rules
            self.save()
            return copy.deepcopy(self.config)

    def to_device_deck_packet(self, active_page: int = 0) -> dict[str, Any]:
        with self._lock:
            pages_out = []
            for p in self.config.get("pages", [])[:6]:
                tiles_out = []
                for t in p.get("tiles", [])[:6]:
                    is_tog = bool(t.get("tog", False))
                    is_on = bool(t.get("on", False))
                    off_sub = str(t.get("off_label", "")).strip() or str(t.get("sub", ""))
                    on_sub = str(t.get("on_label", "")).strip() if is_tog else ""
                    sub = off_sub if is_tog else str(t.get("sub", ""))
                    has_hold = bool(str(t.get("hold_action_type", "")).strip())
                    hold_repeat = bool(t.get("hold_repeat", False))
                    act_t = str(t.get("action_type", "")).strip().lower()
                    act_p = str(t.get("action_payload", "")).strip().lower()
                    sb_t = str(t.get("state_b_action_type", "")).strip().lower()
                    sb_p = str(t.get("state_b_action_payload", "")).strip().lower()
                    dyn = ""
                    if is_tog:
                        if (act_t == "media" and act_p == "mute_mic") or (sb_t == "media" and sb_p == "mute_mic"):
                            dyn = "mic"
                        elif (act_t == "media" and act_p in ("play_pause", "play")) or (sb_t == "media" and sb_p in ("play_pause", "play")):
                            dyn = "play"
                        elif (act_t == "media" and act_p in ("mute", "mute_vol")) or (sb_t == "media" and sb_p in ("mute", "mute_vol")):
                            dyn = "mute"
                    tiles_out.append({
                        "label": str(t.get("label", ""))[:17],
                        "sub": sub[:13],
                        "sub2": on_sub[:13],
                        "icon": str(t.get("icon", "bolt"))[:15],
                        "ico2": str(t.get("state_b_icon", "")).strip()[:15],
                        "col": str(t.get("col", "#00F0DA")),
                        "col2": str(t.get("state_b_col", "")).strip(),
                        "dyn": dyn,
                        "tog": is_tog,
                        "on": is_on,
                        "hh": has_hold,
                        "hr": hold_repeat,
                        "jmp": int(t.get("jmp", -1)),
                    })
                pages_out.append({
                    "name": str(p.get("name", "PAGE"))[:13],
                    "tiles": tiles_out,
                })
            return {"t": "deck", "page": active_page, "pages": pages_out}

    def export_json(self) -> str:
        with self._lock:
            return json.dumps(self.config, indent=2)

    def import_json(self, raw: str) -> dict[str, Any]:
        parsed = json.loads(raw)
        if not isinstance(parsed, dict) or "pages" not in parsed:
            raise ValueError("Invalid HYDRO-DECK config JSON: missing 'pages'")
        with self._lock:
            merged = copy.deepcopy(default_config())
            merged.update(parsed)
            self.config = merged
            self.save()
            return self.config

