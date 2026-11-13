(() => {
  "use strict";

  const PALETTE = [
    "#00F0DA", "#3898FF", "#22E88A", "#A866FF",
    "#FFB62E", "#FF3A62", "#FF70D8", "#F4F7FF"
  ];

  const ICONS = {
    mic: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="2" width="6" height="11" rx="3"/><path d="M5 10a7 7 0 0 0 14 0"/><line x1="12" y1="17" x2="12" y2="22"/><line x1="8" y1="22" x2="16" y2="22"/></svg>',
    mic_off: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="2" y1="2" x2="22" y2="22"/><path d="M9 5v4a3 3 0 0 0 5.12 2.12"/><path d="M15 9.34V5a3 3 0 0 0-5.94-.6"/><path d="M17 16.95A7 7 0 0 1 5 10"/><path d="M19 10v1a7 7 0 0 1-.11 1.23"/><line x1="12" y1="19" x2="12" y2="22"/></svg>',
    shot: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 2v14a2 2 0 0 0 2 2h14"/><path d="M18 22V8a2 2 0 0 0-2-2H2"/><circle cx="12" cy="12" r="2.5"/></svg>',
    play: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="6 4 20 12 6 20 6 4"/></svg>',
    pause: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="4" width="4" height="16"/><rect x="14" y="4" width="4" height="16"/></svg>',
    next: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="5 4 15 12 5 20 5 4"/><line x1="19" y1="5" x2="19" y2="19"/></svg>',
    prev: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="19 20 9 12 19 4 19 20"/><line x1="5" y1="19" x2="5" y2="5"/></svg>',
    vol_up: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/><path d="M19.07 4.93a10 10 0 0 1 0 14.14"/></svg>',
    vol_dn: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/></svg>',
    mute: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><line x1="23" y1="9" x2="17" y2="15"/><line x1="17" y1="9" x2="23" y2="15"/></svg>',
    terminal: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>',
    code: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>',
    git: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="18" cy="18" r="3"/><circle cx="6" cy="6" r="3"/><path d="M13 6h3a2 2 0 0 1 2 2v7"/><line x1="6" y1="9" x2="6" y2="21"/></svg>',
    browser: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg>',
    discord: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3z"/><path d="M3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"/></svg>',
    cam: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="23 7 16 12 23 17 23 7"/><rect x="1" y="5" width="15" height="14" rx="2"/></svg>',
    rec: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4" fill="currentColor"/></svg>',
    lock: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>',
    rocket: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4.5 16.5c-1.5 1.26-2 5-2 5s3.74-.5 5-2c.71-.84.7-2.13-.09-2.91a2.18 2.18 0 0 0-2.91-.09z"/><path d="m12 15-3-3a22 22 0 0 1 2-3.95A12.88 12.88 0 0 1 22 2c0 2.72-.78 7.5-6 11a22.35 22.35 0 0 1-4 2z"/><path d="M9 12H4s.55-3.03 2-4c1.62-1.08 5 0 5 0"/><path d="M12 15v5s3.03-.55 4-2c1.08-1.62 0-5 0-5"/></svg>',
    bolt: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>',
    cpu: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><line x1="9" y1="1" x2="9" y2="4"/><line x1="15" y1="1" x2="15" y2="4"/><line x1="9" y1="20" x2="9" y2="23"/><line x1="15" y1="20" x2="15" y2="23"/><line x1="20" y1="9" x2="23" y2="9"/><line x1="20" y1="14" x2="23" y2="14"/><line x1="1" y1="9" x2="4" y2="9"/><line x1="1" y1="14" x2="4" y2="14"/></svg>',
    folder: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg>',
    back: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 14 4 9 9 4"/><path d="M20 20v-7a4 4 0 0 0-4-4H4"/></svg>',
    webhook: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 16.98h-5.99c-1.1 0-1.95.94-2.48 1.9A4 4 0 0 1 2 17c.01-.7.2-1.4.57-2"/><path d="m6 17 3.13-5.78c.53-.97.1-2.18-.5-3.1a4 4 0 1 1 6.89-4.06"/></svg>',
    flame: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8.5 14.5A2.5 2.5 0 0 0 11 12c0-1.38-.5-2-1-3-1.072-2.143-.224-4.054 2-6 .5 2.5 2 4.9 4 6.5 2 1.6 3 3.5 3 5.5a7 7 0 1 1-14 0c0-1.153.433-2.294 1-3a2.5 2.5 0 0 0 2.5 2.5z"/></svg>',
    window: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2"/><line x1="3" y1="9" x2="21" y2="9"/><line x1="12" y1="9" x2="12" y2="21"/></svg>',
    music: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>',
    clipboard: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="8" y="2" width="8" height="4" rx="1"/><path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/><line x1="9" y1="12" x2="15" y2="12"/><line x1="9" y1="16" x2="13" y2="16"/></svg>',
    settings: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9c.26.6.82 1 1.51 1H21a2 2 0 1 1 0 4h-.09c-.69 0-1.25.4-1.51 1z"/></svg>',
  };

  const QUICK_HOTKEYS = [
    ["CTRL+SHIFT+P", "Cmd Pal"],
    ["CTRL+SHIFT+M", "Mute"],
    ["WIN+SHIFT+S", "Snip"],
    ["CTRL+C", "Copy"],
    ["CTRL+V", "Paste"],
    ["ALT+TAB", "Alt+Tab"],
    ["F13", "F13"],
    ["F14", "F14"],
  ];

  const CODE_TOKEN_MAP = {
    Minus: "-",
    Equal: "=",
    Space: "SPACE",
    Enter: "ENTER",
    NumpadEnter: "ENTER",
    Tab: "TAB",
    Escape: "ESC",
    Backspace: "BACKSPACE",
    Delete: "DELETE",
    Insert: "INSERT",
    Home: "HOME",
    End: "END",
    PageUp: "PGUP",
    PageDown: "PGDN",
    ArrowUp: "UP",
    ArrowDown: "DOWN",
    ArrowLeft: "LEFT",
    ArrowRight: "RIGHT",
    Backquote: "`",
    BracketLeft: "[",
    BracketRight: "]",
    Backslash: "\\",
    Semicolon: ";",
    Quote: "'",
    Comma: ",",
    Period: ".",
    Slash: "/",
    PrintScreen: "PRTSC",
  };

  const MODIFIER_CODES = new Set([
    "ControlLeft", "ControlRight",
    "ShiftLeft", "ShiftRight",
    "AltLeft", "AltRight",
    "MetaLeft", "MetaRight",
  ]);

  let config = null;
  let activePage = 0;
  let selectedTile = 0;
  let activeIcon = "bolt";
  let inspectorDirty = false;
  let cfgDirty = false;
  let latestStats = {};
  let telemHistory = [];
  let chartMode = "util";
  let latestSerial = {};
  let latestBins = {};
  let latestVCyd = {};
  let dragSourceSlot = null;
  let recordingTarget = null; // null | { inputEl, btnEl, rowEl, onDone }
  let activeModalConfirm = null;
  let twinZoom = false;
  let studioAudioCtx = null;

  let vcydWs = null;
  let vcydPointerDown = false;
  let vcydCanvasCtx = null;
  let vcydImageData = null;
  let vcydFrames = 0;
  let vcydLastFpsTs = performance.now();

  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => Array.from(document.querySelectorAll(sel));

  function esc(str) {
    return String(str ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function showToast(msg, duration = 2000) {
    const el = $("#studio-toast");
    if (!el) return;
    el.textContent = msg;
    el.classList.add("show");
    clearTimeout(el._tid);
    el._tid = setTimeout(() => el.classList.remove("show"), duration);
  }

  async function apiJson(url, opts = {}) {
    const res = await fetch(url, {
      headers: { "Content-Type": "application/json", ...(opts.headers || {}) },
      ...opts,
    });
    const text = await res.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch (_) {}
    if (!res.ok) {
      throw new Error(data.detail || `HTTP ${res.status}`);
    }
    return data;
  }

  function setDirty(flag) {
    inspectorDirty = !!flag;
    const pill = $("#insp-dirty");
    if (pill) pill.style.display = inspectorDirty ? "inline-flex" : "none";
  }

  function setCfgDirty(flag) {
    cfgDirty = !!flag;
    const pill = $("#cfg-dirty");
    if (!pill) return;
    pill.textContent = cfgDirty ? "Unsaved Changes" : "Config Synced";
    pill.classList.toggle("dirty", cfgDirty);
  }

  function cancelKeyRecording() {
    if (!recordingTarget) return;
    const { btnEl, rowEl } = recordingTarget;
    recordingTarget = null;
    if (btnEl) {
      btnEl.classList.remove("recording");
      btnEl.textContent = btnEl.dataset.idleLabel || "Record";
    }
    if (rowEl) rowEl.classList.remove("is-recording");
  }

  function evCodeToToken(ev) {
    const c = ev.code || "";
    if (c.startsWith("Key") && c.length === 4) return c.slice(3).toUpperCase();
    if (c.startsWith("Digit") && c.length === 6) return c.slice(5);
    if (c.startsWith("Numpad") && /^Numpad\d$/.test(c)) return "NUM" + c.slice(6);
    if (/^F([1-9]|1\d|2[0-4])$/.test(c)) return c;
    if (CODE_TOKEN_MAP[c]) return CODE_TOKEN_MAP[c];
    return null;
  }

  function buildComboFromKeyEvent(ev) {
    if (MODIFIER_CODES.has(ev.code)) return null;
    const keyTok = evCodeToToken(ev);
    if (!keyTok) return null;
    const mods = [];
    if (ev.ctrlKey) mods.push("CTRL");
    if (ev.shiftKey) mods.push("SHIFT");
    if (ev.altKey) mods.push("ALT");
    if (ev.metaKey) mods.push("WIN");
    mods.push(keyTok);
    return mods.join("+");
  }

  /* ---------- TABS & MODALS ---------- */

  function switchTab(tabName) {
    cancelKeyRecording();
    if (config && inspectorDirty && tabName !== "deck") {
      flushDirtyInspector().catch(() => {});
    }
    $$(".nav-item").forEach((b) => b.classList.toggle("active", b.dataset.tab === tabName));
    $$(".tab-view").forEach((v) => v.classList.toggle("active", v.id === `tab-${tabName}`));
    if (tabName === "vitals") {
      requestAnimationFrame(() => renderTelemetryChart());
    }
  }

  function openTextModal({ title, label, value = "", placeholder = "", onConfirm }) {
    $("#modal-title").textContent = title;
    $("#modal-label").textContent = label;
    const inp = $("#modal-input");
    inp.style.display = "block";
    inp.value = value;
    inp.placeholder = placeholder;
    const confirmBtn = $("#modal-confirm");
    confirmBtn.textContent = "Confirm";
    confirmBtn.className = "btn btn-primary btn-sm";
    activeModalConfirm = onConfirm;
    $("#studio-modal").style.display = "grid";
    setTimeout(() => { inp.focus(); inp.select(); }, 20);
  }

  function openConfirmModal({ title, message, confirmText = "Confirm", danger = true, onConfirm }) {
    $("#modal-title").textContent = title;
    $("#modal-label").textContent = message;
    const inp = $("#modal-input");
    inp.style.display = "none";
    inp.value = "";
    const confirmBtn = $("#modal-confirm");
    confirmBtn.textContent = confirmText;
    confirmBtn.className = danger ? "btn btn-danger btn-sm" : "btn btn-primary btn-sm";
    activeModalConfirm = onConfirm;
    $("#studio-modal").style.display = "grid";
    setTimeout(() => confirmBtn.focus(), 20);
  }

  function closeTextModal() {
    $("#studio-modal").style.display = "none";
    activeModalConfirm = null;
  }

  async function openTemplatesModal() {
    $("#templates-modal").style.display = "grid";
    const list = $("#tpl-list");
    list.innerHTML = `<div class="muted-text">Loading starter packs...</div>`;
    try {
      const data = await apiJson("/api/templates");
      const items = data.templates || [];
      list.innerHTML = items.map((t) => `
        <div class="tpl-card" data-tid="${esc(t.id)}">
          <div>
            <div class="tpl-title">${esc(t.title)}</div>
            <div class="muted-text" style="margin-top:4px;">${esc(t.description)}</div>
            <div class="mono muted-text" style="margin-top:6px;">Page: ${esc(t.page_name)} • ${t.tiles} Keys</div>
          </div>
          <div class="tpl-actions">
            <button type="button" class="btn btn-accent btn-sm tpl-apply" data-tid="${esc(t.id)}" data-mode="append">+ Add Page</button>
            <button type="button" class="btn btn-ghost btn-sm tpl-apply" data-tid="${esc(t.id)}" data-mode="replace">Replace All</button>
          </div>
        </div>
      `).join("");
      list.querySelectorAll(".tpl-apply").forEach((btn) => {
        btn.addEventListener("click", async () => {
          try {
            const resp = await apiJson("/api/templates/apply", {
              method: "POST",
              body: JSON.stringify({ template_id: btn.dataset.tid, mode: btn.dataset.mode }),
            });
            config = resp.config;
            activePage = resp.active_page ?? 0;
            selectedTile = 0;
            setDirty(false);
            $("#templates-modal").style.display = "none";
            renderAll();
            showToast(`Loaded template: ${config.pages[activePage]?.name || "OK"}`);
          } catch (err) {
            showToast(err.message);
          }
        });
      });
    } catch (err) {
      list.innerHTML = `<div class="muted-text">Failed to load templates: ${esc(err.message)}</div>`;
    }
  }

  /* ---------- PAGE & DECK MATRIX ---------- */

  function ensureTileDefaults(t) {
    return {
      label: "NEW KEY",
      sub: "",
      icon: "bolt",
      col: "#00F0DA",
      tog: false,
      on: false,
      off_label: "",
      on_label: "",
      state_b_action_type: "",
      state_b_action_payload: "",
      jmp: -1,
      action_type: "hotkey",
      action_payload: "",
      hold_action_type: "",
      hold_action_payload: "",
      hold_repeat: false,
      ...t,
    };
  }

  function renderPageTabs() {
    const pages = config?.pages || [];
    const tabs = $("#page-tabs");
    tabs.innerHTML = pages.map((p, idx) => `
      <button type="button" class="page-tab ${idx === activePage ? "active" : ""}" data-page="${idx}">
        ${idx + 1}. ${esc(p.name)}
      </button>
    `).join("");

    tabs.querySelectorAll(".page-tab").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const nextIdx = Number(btn.dataset.page);
        if (nextIdx === activePage) return;
        await flushDirtyInspector();
        activePage = nextIdx;
        selectedTile = 0;
        renderPageTabs();
        renderDeckGrid();
        loadInspector();
        apiJson("/api/cyd/touch_sim", {
          method: "POST",
          body: JSON.stringify({ screen: "deck", page: activePage }),
        }).catch(() => {});
      });
    });

    const delBtn = $("#btn-del-page");
    if (delBtn) delBtn.disabled = pages.length <= 1;
  }

  function getTileDisplaySub(tile) {
    if (tile.tog) {
      if (tile.on && tile.on_label) return tile.on_label;
      if (!tile.on && tile.off_label) return tile.off_label;
    }
    return tile.sub || "";
  }

  function renderDeckGrid() {
    const grid = $("#deck-grid");
    const page = config?.pages?.[activePage];
    if (!page) {
      grid.innerHTML = "";
      return;
    }
    grid.innerHTML = (page.tiles || []).slice(0, 6).map((raw, idx) => {
      const tile = ensureTileDefaults(raw);
      const col = tile.col || "#00F0DA";
      const isSelected = idx === selectedTile;
      const hasHold = !!(tile.hold_action_type && tile.hold_action_type.trim());
      const bgTint = tile.tog && tile.on ? `background: linear-gradient(180deg, ${col}26, ${col}10); border-color:${col};` : "";
      let iconName = tile.icon || "bolt";
      if (tile.tog && tile.on) {
        if (iconName === "mic") iconName = "mic_off";
        else if (iconName === "play") iconName = "pause";
        else if (iconName === "vol_dn" || iconName === "vol_up") iconName = "mute";
      }
      const ico = ICONS[iconName] || ICONS.bolt;
      return `
        <div class="deck-key ${isSelected ? "selected" : ""}" draggable="true" data-slot="${idx}" style="${bgTint}">
          <div class="dk-accent" style="background:${col};box-shadow:0 0 8px ${col};"></div>
          ${hasHold ? `<span class="dk-hold-tag">H</span>` : ""}
          ${tile.hold_repeat ? `<span class="dk-repeat-tag">R</span>` : ""}
          ${tile.tog ? `<span class="dk-badge" style="background:${tile.on ? col : "#26334c"};box-shadow:${tile.on ? `0 0 8px ${col}` : "none"};"></span>` : ""}
          <div class="dk-icon" style="color:${col};">${ico}</div>
          <div class="dk-label">${esc(tile.label)}</div>
          <div class="dk-sub">${esc(getTileDisplaySub(tile) || tile.action_type)}</div>
        </div>
      `;
    }).join("");

    grid.querySelectorAll(".deck-key").forEach((el) => {
      const slot = Number(el.dataset.slot);
      el.addEventListener("click", async () => {
        if (slot !== selectedTile) {
          cancelKeyRecording();
          await flushDirtyInspector();
          selectedTile = slot;
          renderDeckGrid();
          loadInspector();
        }
      });
      el.addEventListener("dblclick", async () => {
        selectedTile = slot;
        await flushDirtyInspector();
        testTile(activePage, slot);
      });
      el.addEventListener("dragstart", (e) => {
        dragSourceSlot = slot;
        e.dataTransfer.effectAllowed = "move";
        e.dataTransfer.setData("text/plain", String(slot));
      });
      el.addEventListener("dragover", (e) => {
        e.preventDefault();
        el.classList.add("drag-over");
      });
      el.addEventListener("dragleave", () => el.classList.remove("drag-over"));
      el.addEventListener("drop", async (e) => {
        e.preventDefault();
        el.classList.remove("drag-over");
        const from = dragSourceSlot;
        dragSourceSlot = null;
        if (from === null || from === slot) return;
        await flushDirtyInspector();
        const tiles = config.pages[activePage].tiles;
        const tmp = tiles[from];
        tiles[from] = tiles[slot];
        tiles[slot] = tmp;
        selectedTile = slot;
        await persistConfig(`Swapped Slot ${from + 1} ↔ ${slot + 1}`);
        renderDeckGrid();
        loadInspector();
      });
    });
  }

  /* ---------- INSPECTOR & PAYLOAD EDITORS ---------- */

  function renderPaletteSwatches(activeCol) {
    const row = $("#color-swatches");
    row.innerHTML = PALETTE.map((c) => `
      <button type="button" class="swatch ${c.toUpperCase() === activeCol.toUpperCase() ? "active" : ""}"
        data-col="${c}" style="background:${c};" title="${c}"></button>
    `).join("");
    row.querySelectorAll(".swatch").forEach((b) => {
      b.addEventListener("click", () => {
        $("#f-color").value = b.dataset.col;
        renderPaletteSwatches(b.dataset.col);
        setDirty(true);
      });
    });
  }

  function renderIconPicker(selected) {
    activeIcon = selected in ICONS ? selected : "bolt";
    const grid = $("#icon-grid");
    grid.innerHTML = Object.entries(ICONS).map(([key, svg]) => `
      <button type="button" class="icon-opt ${key === activeIcon ? "active" : ""}" data-ico="${key}" title="${key}">
        <div style="width:16px;height:16px;color:${key === activeIcon ? "var(--cyan)" : "var(--text-pri)"};">${svg}</div>
      </button>
    `).join("");
    grid.querySelectorAll(".icon-opt").forEach((b) => {
      b.addEventListener("click", () => {
        activeIcon = b.dataset.ico;
        renderIconPicker(activeIcon);
        setDirty(true);
      });
    });
  }

  function buildSinglePayloadEditor(container, type, rawPayload, scope) {
    const pStr = typeof rawPayload === "object" && rawPayload !== null ? "" : String(rawPayload ?? "");
    const pObj = typeof rawPayload === "object" && rawPayload !== null ? rawPayload : {};

    if (!type) {
      const msg = scope === "state_b"
        ? "Uses Primary Tap Action when Toggle is ON."
        : "No secondary action bound to long-press.";
      container.innerHTML = `<div class="muted-text">${msg}</div>`;
      return;
    }

    if (type === "hotkey") {
      container.innerHTML = `
        <label class="field">
          <span>Hotkey Combination</span>
          <div class="hotkey-row">
            <input type="text" class="pl-main mono" value="${esc(pStr)}" placeholder="e.g. CTRL+SHIFT+S" />
            <button type="button" class="btn btn-sm btn-record" data-idle-label="Record">Record</button>
          </div>
        </label>
        <div class="chip-row">
          ${QUICK_HOTKEYS.map(([hk, lbl]) => `<button type="button" class="btn-xs hk-chip" data-hk="${esc(hk)}">${esc(lbl)}</button>`).join("")}
        </div>
      `;
      const inp = container.querySelector(".pl-main");
      const recBtn = container.querySelector(".btn-record");
      inp.addEventListener("input", () => setDirty(true));
      container.querySelectorAll(".hk-chip").forEach((c) => {
        c.addEventListener("click", () => {
          inp.value = c.dataset.hk;
          setDirty(true);
        });
      });
      recBtn.addEventListener("click", () => {
        const same = recordingTarget && recordingTarget.btnEl === recBtn;
        cancelKeyRecording();
        if (!same) {
          recordingTarget = { inputEl: inp, btnEl: recBtn };
          recBtn.classList.add("recording");
          recBtn.textContent = "Press Keys...";
        }
      });
      return;
    }

    if (type === "sequence") {
      let steps = [];
      try {
        steps = Array.isArray(rawPayload) ? JSON.parse(JSON.stringify(rawPayload)) : JSON.parse(pStr || "[]");
      } catch (_) { steps = []; }
      if (!Array.isArray(steps) || steps.length === 0) {
        steps = [{ kind: "hotkey", value: "CTRL+C" }, { kind: "delay", value: 100 }, { kind: "text", value: "done" }];
      }
      container._seqSteps = steps;
      renderSequenceEditor(container);
      return;
    }

    if (type === "clipboard") {
      let cfg = { text: "", paste: false, mode: "type" };
      if (typeof rawPayload === "object" && rawPayload !== null) {
        cfg = { ...cfg, ...rawPayload };
      } else if (pStr.trim().startsWith("{")) {
        try { cfg = { ...cfg, ...JSON.parse(pStr) }; } catch (_) { cfg.text = pStr; }
      } else {
        cfg.text = pStr;
      }
      const tpl = cfg.text ?? cfg.template ?? "";
      const modeVal = cfg.mode === "copy" ? "copy" : (cfg.paste || cfg.mode === "paste" ? "paste" : "type");
      container.innerHTML = `
        <label class="field">
          <span>Clipboard Execution Mode</span>
          <select class="pl-clip-mode">
            <option value="type" ${modeVal === "type" ? "selected" : ""}>Type Expanded Text (Unicode Keystrokes)</option>
            <option value="paste" ${modeVal === "paste" ? "selected" : ""}>Write Clipboard &amp; Paste (Ctrl+V)</option>
            <option value="copy" ${modeVal === "copy" ? "selected" : ""}>Copy to Windows Clipboard Only</option>
          </select>
        </label>
        <label class="field">
          <span>Snippet Template (supports {{date}}, {{time}}, {{datetime}}, {{clipboard}})</span>
          <textarea class="pl-main mono" rows="2" placeholder="[{{datetime}}] {{clipboard}}">${esc(tpl)}</textarea>
        </label>
        <div class="chip-row">
          <button type="button" class="btn-xs tpl-tok" data-tok="{{date}}">+ {{date}}</button>
          <button type="button" class="btn-xs tpl-tok" data-tok="{{time}}">+ {{time}}</button>
          <button type="button" class="btn-xs tpl-tok" data-tok="{{datetime}}">+ {{datetime}}</button>
          <button type="button" class="btn-xs tpl-tok" data-tok="{{clipboard}}">+ {{clipboard}}</button>
        </div>
      `;
      const area = container.querySelector(".pl-main");
      area.addEventListener("input", () => setDirty(true));
      container.querySelector(".pl-clip-mode").addEventListener("change", () => setDirty(true));
      container.querySelectorAll(".tpl-tok").forEach((btn) => {
        btn.addEventListener("click", () => {
          area.value += (area.value && !area.value.endsWith(" ") ? " " : "") + btn.dataset.tok;
          setDirty(true);
          area.focus();
        });
      });
      return;
    }

    if (type === "soundboard") {
      const presets = ["chime", "alert", "success", "fanfare", "beep:880:150", "stop"];
      container.innerHTML = `
        <label class="field">
          <span>WAV/MP3/OGG Path or Built-in Tone Preset</span>
          <input type="text" class="pl-main mono" value="${esc(pStr || "chime")}" placeholder="C:\\Sounds\\airhorn.wav or chime" />
        </label>
        <div class="chip-row">
          ${presets.map((p) => `<button type="button" class="btn-xs sfx-chip" data-v="${esc(p)}">${esc(p)}</button>`).join("")}
        </div>
      `;
      const inp = container.querySelector(".pl-main");
      inp.addEventListener("input", () => setDirty(true));
      container.querySelectorAll(".sfx-chip").forEach((b) => {
        b.addEventListener("click", () => { inp.value = b.dataset.v; setDirty(true); });
      });
      return;
    }

    if (type === "media") {
      const opts = [
        ["mute_mic", "Toggle Microphone Mute"],
        ["play_pause", "Play / Pause Media"],
        ["next", "Next Track"],
        ["prev", "Previous Track"],
        ["vol_up", "Master Volume +6%"],
        ["vol_down", "Master Volume -6%"],
        ["mute_vol", "Mute Master Output"],
      ];
      const cur = (pStr || "play_pause").toLowerCase();
      container.innerHTML = `
        <label class="field">
          <span>Audio / Media Action</span>
          <select class="pl-main">
            ${opts.map(([v, l]) => `<option value="${v}" ${cur === v ? "selected" : ""}>${l}</option>`).join("")}
          </select>
        </label>
      `;
      container.querySelector(".pl-main").addEventListener("change", () => setDirty(true));
      return;
    }

    if (type === "window") {
      const opts = [
        ["snap_left", "Snap Window Left (Win+Left)"],
        ["snap_right", "Snap Window Right (Win+Right)"],
        ["snap_max", "Maximize Window (Win+Up)"],
        ["min_all", "Minimize All Windows (Win+M)"],
        ["lock_pc", "Lock Windows Workstation"],
      ];
      const cur = (pStr || "snap_left").toLowerCase();
      container.innerHTML = `
        <label class="field">
          <span>Window Action</span>
          <select class="pl-main">
            ${opts.map(([v, l]) => `<option value="${v}" ${cur === v ? "selected" : ""}>${l}</option>`).join("")}
          </select>
        </label>
      `;
      container.querySelector(".pl-main").addEventListener("change", () => setDirty(true));
      return;
    }

    if (type === "app_focus") {
      const target = typeof rawPayload === "object" && rawPayload !== null ? (rawPayload.target || "") : pStr;
      const launch = typeof rawPayload === "object" && rawPayload !== null ? (rawPayload.launch || "") : "";
      container.innerHTML = `
        <div class="form-grid-2">
          <label class="field">
            <span>Window Title or Process</span>
            <input type="text" class="pl-target" value="${esc(target)}" placeholder="Code.exe or Discord" />
          </label>
          <label class="field">
            <span>Launch Fallback (optional)</span>
            <input type="text" class="pl-launch" value="${esc(launch)}" placeholder="code or app.exe" />
          </label>
        </div>
      `;
      container.querySelectorAll("input").forEach((el) => el.addEventListener("input", () => setDirty(true)));
      return;
    }

    if (type === "webhook") {
      let cfg = { url: "", method: "POST", body: "{}", headers: {} };
      if (typeof rawPayload === "object" && rawPayload !== null) cfg = { ...cfg, ...rawPayload };
      else if (pStr.trim().startsWith("{")) {
        try { cfg = { ...cfg, ...JSON.parse(pStr) }; } catch (_) { cfg.url = pStr; }
      } else { cfg.url = pStr; }
      const bodyStr = typeof cfg.body === "object" ? JSON.stringify(cfg.body) : String(cfg.body ?? "{}");
      const hdrStr = typeof cfg.headers === "object" && cfg.headers !== null
        ? JSON.stringify(cfg.headers)
        : String(cfg.headers || "{}");
      container.innerHTML = `
        <div class="form-grid-2">
          <label class="field">
            <span>Method</span>
            <select class="pl-method">
              ${["POST", "GET", "PUT", "PATCH", "DELETE"].map((m) => `<option value="${m}" ${cfg.method === m ? "selected" : ""}>${m}</option>`).join("")}
            </select>
          </label>
          <label class="field">
            <span>Endpoint URL</span>
            <input type="url" class="pl-url" value="${esc(cfg.url)}" placeholder="https://example.com/hook" />
          </label>
        </div>
        <label class="field">
          <span>Headers (JSON Object)</span>
          <input type="text" class="pl-headers mono" value="${esc(hdrStr)}" placeholder='{"Authorization":"Bearer ..."}' />
        </label>
        <label class="field">
          <span>JSON Body</span>
          <input type="text" class="pl-body mono" value="${esc(bodyStr)}" placeholder='{"event":"hydro"}' />
        </label>
      `;
      container.querySelectorAll("input,select").forEach((el) => el.addEventListener("input", () => setDirty(true)));
      return;
    }

    if (type === "cyd_screen") {
      const opts = [
        ["deck", "DECK (6-Tile Macro Grid)"],
        ["vitals:overview", "HW VITALS • Overview"],
        ["vitals:cpu", "HW VITALS • CPU Threads"],
        ["vitals:gpu", "HW VITALS • GPU Telemetry"],
        ["vitals:ram", "HW VITALS • RAM & Storage"],
        ["media", "MEDIA (Volume & Now Playing)"],
        ["timer", "TIMER (Pomodoro & Stopwatch)"],
        ["sys", "SYS (Backlight, RGB & Hardware)"],
      ];
      const cur = String(pStr || "deck").toLowerCase();
      container.innerHTML = `
        <label class="field">
          <span>Target CYD Touch Screen</span>
          <select class="pl-main">
            ${opts.map(([v, l]) => `<option value="${v}" ${cur === v ? "selected" : ""}>${l}</option>`).join("")}
          </select>
        </label>
      `;
      container.querySelector(".pl-main").addEventListener("change", () => setDirty(true));
      return;
    }

    if (type === "timer") {
      const opts = [
        ["focus", "Switch CYD to Pomodoro / Timer Screen"],
        ["start", "Open Timer & Prepare Countdown"],
        ["reset", "Open Timer (Reset View)"],
      ];
      const cur = String(pStr || "focus").toLowerCase();
      container.innerHTML = `
        <label class="field">
          <span>CYD Timer Control</span>
          <select class="pl-main">
            ${opts.map(([v, l]) => `<option value="${v}" ${cur === v ? "selected" : ""}>${l}</option>`).join("")}
          </select>
        </label>
      `;
      container.querySelector(".pl-main").addEventListener("change", () => setDirty(true));
      return;
    }

    if (type === "folder") {
      const pages = config?.pages || [];
      const cur = Number(pStr !== "" ? pStr : (config.pages[activePage].tiles[selectedTile]?.jmp ?? 0));
      container.innerHTML = `
        <label class="field">
          <span>Target Deck Page</span>
          <select class="pl-main">
            ${pages.map((p, idx) => `<option value="${idx}" ${idx === cur ? "selected" : ""}>${idx + 1}. ${esc(p.name)}</option>`).join("")}
          </select>
        </label>
      `;
      container.querySelector(".pl-main").addEventListener("change", () => setDirty(true));
      return;
    }

    if (type === "screenshot") {
      container.innerHTML = `<div class="muted-text">Saves full multi-monitor PNG into Pictures\\HYDRO-DECK.</div>`;
      return;
    }

    const placeholders = {
      app: "notepad.exe or ms-settings:sound",
      url: "https://github.com",
      shell: "powershell script or git status -sb",
    };
    container.innerHTML = `
      <label class="field">
        <span>Target / Command Payload</span>
        <input type="text" class="pl-main mono" value="${esc(pStr)}" placeholder="${esc(placeholders[type] || "")}" />
      </label>
    `;
    container.querySelector(".pl-main").addEventListener("input", () => setDirty(true));
  }

  function renderSequenceEditor(container) {
    const steps = container._seqSteps || (container._seqSteps = []);
    const seqKinds = [
      "hotkey", "text", "delay", "clipboard", "soundboard",
      "media", "window", "screenshot", "app", "app_focus", "url", "shell", "webhook"
    ];
    container.innerHTML = `
      <div class="box-head-inline" style="justify-content:space-between;">
        <span class="muted-text">Ordered Execution Steps (${steps.length})</span>
        <button type="button" class="btn-xs" id="btn-seq-add">+ Add Step</button>
      </div>
      <div class="seq-list">
        ${steps.map((s, idx) => `
          <div class="seq-step" data-idx="${idx}">
            <select class="seq-kind">
              ${seqKinds.map((k) => `<option value="${k}" ${s.kind === k ? "selected" : ""}>${k}</option>`).join("")}
            </select>
            <input type="text" class="seq-val mono" value="${esc(typeof s.value === "object" && s.value !== null ? JSON.stringify(s.value) : (s.value ?? ""))}" placeholder="${s.kind === "delay" ? "ms" : "value"}" />
            <button type="button" class="btn-xs seq-rec" data-idle-label="Rec" title="Record keyboard shortcut" ${s.kind === "hotkey" ? "" : 'style="visibility:hidden;"'}>Rec</button>
            <button type="button" class="btn-xs seq-up" title="Move Up">↑</button>
            <button type="button" class="btn-xs seq-dn" title="Move Down">↓</button>
            <button type="button" class="btn-xs seq-del" title="Remove">✕</button>
          </div>
        `).join("")}
      </div>
    `;
    container.querySelector("#btn-seq-add").addEventListener("click", () => {
      steps.push({ kind: "delay", value: 80 });
      setDirty(true);
      renderSequenceEditor(container);
    });
    container.querySelectorAll(".seq-step").forEach((row) => {
      const idx = Number(row.dataset.idx);
      const valInp = row.querySelector(".seq-val");
      const recBtn = row.querySelector(".seq-rec");
      row.querySelector(".seq-kind").addEventListener("change", (e) => {
        const nk = e.target.value;
        steps[idx].kind = nk;
        recBtn.style.visibility = nk === "hotkey" ? "visible" : "hidden";
        valInp.placeholder = nk === "delay" ? "ms" : "value";
        cancelKeyRecording();
        setDirty(true);
      });
      valInp.addEventListener("input", (e) => {
        const k = steps[idx].kind;
        steps[idx].value = k === "delay" ? Number(e.target.value || 0) : e.target.value;
        setDirty(true);
      });
      recBtn.addEventListener("click", () => {
        const same = recordingTarget && recordingTarget.btnEl === recBtn;
        cancelKeyRecording();
        if (!same) {
          row.classList.add("is-recording");
          recBtn.classList.add("recording");
          recBtn.textContent = "...";
          recordingTarget = {
            inputEl: valInp,
            btnEl: recBtn,
            rowEl: row,
            onDone: (combo) => { steps[idx].value = combo; },
          };
        }
      });
      row.querySelector(".seq-up").addEventListener("click", () => {
        if (idx <= 0) return;
        cancelKeyRecording();
        [steps[idx - 1], steps[idx]] = [steps[idx], steps[idx - 1]];
        setDirty(true);
        renderSequenceEditor(container);
      });
      row.querySelector(".seq-dn").addEventListener("click", () => {
        if (idx >= steps.length - 1) return;
        cancelKeyRecording();
        [steps[idx + 1], steps[idx]] = [steps[idx], steps[idx + 1]];
        setDirty(true);
        renderSequenceEditor(container);
      });
      row.querySelector(".seq-del").addEventListener("click", () => {
        cancelKeyRecording();
        steps.splice(idx, 1);
        setDirty(true);
        renderSequenceEditor(container);
      });
    });
  }

  function extractPayloadFromEditor(container, type) {
    if (!type) return "";
    if (type === "sequence") return JSON.parse(JSON.stringify(container._seqSteps || []));
    if (type === "screenshot") return "";
    if (type === "app_focus") {
      return {
        target: container.querySelector(".pl-target")?.value.trim() || "",
        launch: container.querySelector(".pl-launch")?.value.trim() || "",
      };
    }
    if (type === "webhook") {
      const method = container.querySelector(".pl-method")?.value || "POST";
      const url = container.querySelector(".pl-url")?.value.trim() || "";
      const bodyRaw = container.querySelector(".pl-body")?.value || "{}";
      const hdrRaw = (container.querySelector(".pl-headers")?.value || "{}").trim();
      let headers = {};
      if (hdrRaw) {
        try {
          const parsed = JSON.parse(hdrRaw);
          if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) headers = parsed;
        } catch (_) { headers = hdrRaw; }
      }
      return { method, url, body: bodyRaw, headers };
    }
    if (type === "clipboard") {
      const text = container.querySelector(".pl-main")?.value || "";
      const mode = container.querySelector(".pl-clip-mode")?.value || "type";
      return { text, paste: mode === "paste", mode };
    }
    return container.querySelector(".pl-main")?.value.trim() ?? "";
  }

  function loadInspector() {
    cancelKeyRecording();
    const page = config?.pages?.[activePage];
    const tile = ensureTileDefaults(page?.tiles?.[selectedTile] || {});
    $("#insp-slot").textContent = `PAGE ${activePage + 1} (${page?.name || ""}) // SLOT ${selectedTile + 1}`;
    $("#f-label").value = tile.label;
    $("#f-sub").value = tile.sub;
    $("#f-color").value = tile.col || "#00F0DA";
    $("#f-tog").checked = !!tile.tog;
    $("#ab-labels-row").style.display = tile.tog ? "grid" : "none";
    $("#state-b-action-wrap").style.display = tile.tog ? "flex" : "none";
    $("#f-off-label").value = tile.off_label || "";
    $("#f-on-label").value = tile.on_label || "";
    $("#f-sb-type").value = tile.state_b_action_type || "";
    buildSinglePayloadEditor($("#state-b-payload-area"), $("#f-sb-type").value, tile.state_b_action_payload, "state_b");

    renderPaletteSwatches(tile.col || "#00F0DA");
    renderIconPicker(tile.icon || "bolt");

    $("#f-atype").value = tile.action_type || "hotkey";
    buildSinglePayloadEditor($("#payload-area"), $("#f-atype").value, tile.action_payload, "primary");

    $("#f-hrepeat").checked = !!tile.hold_repeat;
    $("#f-htype").value = tile.hold_action_type || "";
    buildSinglePayloadEditor($("#hold-payload-area"), $("#f-htype").value, tile.hold_action_payload, "hold");

    setDirty(false);
  }

  function collectInspectorIntoTile() {
    const page = config?.pages?.[activePage];
    if (!page) return;
    const prev = ensureTileDefaults(page.tiles[selectedTile] || {});
    const atype = $("#f-atype").value;
    const apayload = extractPayloadFromEditor($("#payload-area"), atype);
    const htype = $("#f-htype").value;
    const hpayload = extractPayloadFromEditor($("#hold-payload-area"), htype);
    const sbtype = $("#f-sb-type").value;
    const sbpayload = extractPayloadFromEditor($("#state-b-payload-area"), sbtype);

    page.tiles[selectedTile] = {
      ...prev,
      label: ($("#f-label").value.trim() || "KEY").slice(0, 16),
      sub: $("#f-sub").value.trim().slice(0, 12),
      icon: activeIcon,
      col: $("#f-color").value || "#00F0DA",
      tog: !!$("#f-tog").checked,
      off_label: $("#f-off-label").value.trim().slice(0, 12),
      on_label: $("#f-on-label").value.trim().slice(0, 12),
      state_b_action_type: sbtype,
      state_b_action_payload: sbpayload,
      action_type: atype,
      action_payload: apayload,
      jmp: atype === "folder" ? Number(apayload || 0) : -1,
      hold_action_type: htype,
      hold_action_payload: hpayload,
      hold_repeat: !!$("#f-hrepeat").checked,
    };
  }

  async function persistConfig(toastMsg = "Configuration synced") {
    const res = await apiJson("/api/config", {
      method: "POST",
      body: JSON.stringify(config),
    });
    if (res.config) config = res.config;
    setDirty(false);
    setCfgDirty(false);
    if (toastMsg) showToast(toastMsg);
  }

  async function flushDirtyInspector() {
    if (!inspectorDirty || !config) return;
    collectInspectorIntoTile();
    await persistConfig("Auto-saved key changes");
  }

  async function testTile(pageIdx, tileIdx, hold = false) {
    try {
      const res = await apiJson("/api/macro/test", {
        method: "POST",
        body: JSON.stringify({ page: pageIdx, tile: tileIdx, hold: !!hold }),
      });
      showToast(res.toast || res.detail || (hold ? "Hold macro triggered" : "Macro triggered"));
    } catch (err) {
      showToast(err.message);
    }
  }

  /* ---------- MACRO HISTORY & AUDIO RAIL ---------- */

  function renderMacroLog(items) {
    const log = $("#macro-log");
    if (!items || !items.length) {
      log.innerHTML = `<div class="log-empty">No macros triggered yet this session.</div>`;
      return;
    }
    log.innerHTML = items.slice(0, 12).map((r) => `
      <div class="log-row">
        <span class="mono" style="color:${r.ok ? "var(--emerald)" : "var(--crimson)"}">[${esc(r.ts)}] ${esc(r.action_type)}</span>
        <span class="muted-text truncate">${esc(r.detail)}</span>
      </div>
    `).join("");
  }

  /* ---------- VITALS & SVG TELEMETRY CHART ---------- */

  function setRing(el, pct) {
    if (!el) return;
    const clamped = Math.max(0, Math.min(100, Number(pct) || 0));
    const arcLen = (clamped / 100) * 226;
    el.setAttribute("stroke-dasharray", `${arcLen.toFixed(1)} 302`);
  }

  function renderTelemetryChart() {
    const svg = $("#telem-svg");
    if (!svg) return;
    const rect = svg.getBoundingClientRect();
    const W = Math.max(360, Math.round(rect.width || 620));
    const H = Math.max(150, Math.round(rect.height || 185));
    svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
    const L = 38, R = 12, T = 14, B = 24;
    const PW = W - L - R, PH = H - T - B;
    const hist = telemHistory.slice(-48);

    const legend = $("#chart-legend");
    if (legend) {
      legend.innerHTML = chartMode === "util"
        ? `<span class="leg cyan">● CPU</span><span class="leg emerald">● GPU</span><span class="leg violet">● RAM</span>`
        : `<span class="leg cyan">● DOWN Mbps</span><span class="leg amber" style="color:var(--amber)">● UP Mbps</span>`;
    }

    let maxY = 100;
    if (chartMode === "net") {
      const peak = Math.max(5, ...hist.map((h) => Math.max(Number(h.ndn || 0), Number(h.nup || 0))));
      maxY = Math.ceil(peak / 5) * 5;
    }

    const gridLines = [0, 0.25, 0.5, 0.75, 1].map((frac) => {
      const y = (T + PH * (1 - frac)).toFixed(1);
      const val = Math.round(maxY * frac);
      return `
        <line x1="${L}" y1="${y}" x2="${W - R}" y2="${y}" stroke="#192338" stroke-width="1"/>
        <text x="${L - 6}" y="${Number(y) + 3}" text-anchor="end" fill="#7486a6" font-size="10" font-family="Cascadia Code, monospace">${val}${chartMode === "util" ? "%" : ""}</text>
      `;
    }).join("");

    const makePath = (vals) => {
      if (vals.length < 2) return { line: "", area: "", last: null };
      const pts = vals.map((v, idx) => {
        const x = L + (idx / Math.max(1, vals.length - 1)) * PW;
        const y = T + PH * (1 - Math.max(0, Math.min(1, v / maxY)));
        return [x.toFixed(1), y.toFixed(1)];
      });
      const line = "M" + pts.map((p) => p.join(",")).join(" L");
      const area = `${line} L${pts[pts.length - 1][0]},${T + PH} L${pts[0][0]},${T + PH} Z`;
      return { line, area, last: pts[pts.length - 1] };
    };

    const defs = `
      <defs>
        <linearGradient id="gCyan" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#00f0da" stop-opacity="0.28"/><stop offset="100%" stop-color="#00f0da" stop-opacity="0"/></linearGradient>
        <linearGradient id="gEm" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#22e88a" stop-opacity="0.24"/><stop offset="100%" stop-color="#22e88a" stop-opacity="0"/></linearGradient>
        <linearGradient id="gVio" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#a866ff" stop-opacity="0.18"/><stop offset="100%" stop-color="#a866ff" stop-opacity="0"/></linearGradient>
        <linearGradient id="gAmb" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#ffb62e" stop-opacity="0.24"/><stop offset="100%" stop-color="#ffb62e" stop-opacity="0"/></linearGradient>
      </defs>
    `;

    let seriesHtml = "";
    const drawDot = (pt, col) => pt ? `<circle cx="${pt[0]}" cy="${pt[1]}" r="4.5" fill="${col}" fill-opacity="0.24"/><circle cx="${pt[0]}" cy="${pt[1]}" r="2.6" fill="${col}"/>` : "";
    if (chartMode === "util") {
      const sCpu = makePath(hist.map((h) => Number(h.cpu || 0)));
      const sGpu = makePath(hist.map((h) => Number(h.gpu || 0)));
      const sRam = makePath(hist.map((h) => Number(h.ram || 0)));
      if (sRam.line) seriesHtml += `<path d="${sRam.area}" fill="url(#gVio)"/><path d="${sRam.line}" fill="none" stroke="#a866ff" stroke-width="2"/>${drawDot(sRam.last, "#a866ff")}`;
      if (sGpu.line) seriesHtml += `<path d="${sGpu.area}" fill="url(#gEm)"/><path d="${sGpu.line}" fill="none" stroke="#22e88a" stroke-width="2"/>${drawDot(sGpu.last, "#22e88a")}`;
      if (sCpu.line) seriesHtml += `<path d="${sCpu.area}" fill="url(#gCyan)"/><path d="${sCpu.line}" fill="none" stroke="#00f0da" stroke-width="2.4"/>${drawDot(sCpu.last, "#00f0da")}`;
    } else {
      const sDn = makePath(hist.map((h) => Number(h.ndn || 0)));
      const sUp = makePath(hist.map((h) => Number(h.nup || 0)));
      if (sUp.line) seriesHtml += `<path d="${sUp.area}" fill="url(#gAmb)"/><path d="${sUp.line}" fill="none" stroke="#ffb62e" stroke-width="2"/>${drawDot(sUp.last, "#ffb62e")}`;
      if (sDn.line) seriesHtml += `<path d="${sDn.area}" fill="url(#gCyan)"/><path d="${sDn.line}" fill="none" stroke="#00f0da" stroke-width="2.4"/>${drawDot(sDn.last, "#00f0da")}`;
    }

    const xAxis = `
      <text x="${L}" y="${H - 6}" fill="#7486a6" font-size="10" font-family="Cascadia Code, monospace">-48s</text>
      <text x="${L + PW / 2}" y="${H - 6}" text-anchor="middle" fill="#7486a6" font-size="10" font-family="Cascadia Code, monospace">-24s</text>
      <text x="${W - R}" y="${H - 6}" text-anchor="end" fill="#94a5c4" font-size="10" font-family="Cascadia Code, monospace">NOW</text>
    `;

    svg.innerHTML = defs + gridLines + seriesHtml + xAxis;
  }

  function renderStats(stats) {
    if (!stats || typeof stats !== "object") return;
    latestStats = stats;

    const rawClk = Number(stats.cclk || 0);
    const ghzStr = (rawClk > 50 ? rawClk / 1000 : rawClk).toFixed(2);
    $("#host-sub").textContent = `${stats.host || "WORKSTATION"} // E32R28T 2.8"`;
    $("#top-cpu").textContent = `${stats.cpu ?? 0}%`;
    $("#top-cclk").textContent = `${ghzStr} GHz`;
    const hasGpu = Number(stats.vtot || 0) > 0;
    $("#top-gpu").textContent = hasGpu ? `${stats.gpu ?? 0}%` : "N/A";
    $("#top-gtmp").textContent = Number(stats.gtmp ?? -1) > 0 ? `${stats.gtmp}°C` : (hasGpu ? "Idle" : "No NVML");
    $("#top-ram").textContent = `${stats.ram ?? 0}%`;
    $("#top-ruse").textContent = `${Number(stats.ruse || 0).toFixed(1)}/${Number(stats.rtot || 0).toFixed(0)} GB`;

    $("#rail-vol").textContent = `${stats.vol ?? 0}%${stats.vmute ? " (MUTE)" : ""}`;
    if (document.activeElement !== $("#rail-vol-slider")) {
      $("#rail-vol-slider").value = stats.vol ?? 50;
    }
    $("#rail-mic-btn").classList.toggle("active", !stats.mmute);
    $("#rail-mic-btn").textContent = stats.mmute ? "Mic Muted" : "Mic Live";
    $("#rail-play-btn").classList.toggle("active", !!stats.mplay);
    $("#rail-play-btn").textContent = stats.mplay ? "Pause Media" : "Play Media";

    if (config?.pages) {
      let deckChanged = false;
      config.pages.forEach((p, pIdx) => {
        (p.tiles || []).forEach((t) => {
          if (!t.tog) return;
          if (t.action_type === "media" && t.action_payload === "mute_mic" && Boolean(t.on) !== Boolean(stats.mmute)) {
            t.on = Boolean(stats.mmute);
            if (pIdx === activePage) deckChanged = true;
          } else if (t.action_type === "media" && t.action_payload === "play_pause" && Boolean(t.on) !== Boolean(stats.mplay)) {
            t.on = Boolean(stats.mplay);
            if (pIdx === activePage) deckChanged = true;
          }
        });
      });
      if (deckChanged) renderDeckGrid();
    }

    // Vitals Tab Cards
    $("#v-cpu-name").textContent = stats.cpun || "--";
    setRing($("#ring-cpu"), stats.cpu ?? 0);
    $("#v-cpu-pct").textContent = `${stats.cpu ?? 0}%`;
    $("#v-cpu-clk").textContent = `${ghzStr} GHz`;
    $("#v-cpu-proc").textContent = stats.cproc || "-";
    const allCores = stats.all_cores || stats.cores || [];
    $("#v-cpu-cores").textContent = `${stats.physical_cores || "?"}C / ${allCores.length}T`;
    $("#v-cpu-temp-note").textContent = Number(stats.ctmp ?? -1) > 0
      ? `Package ${stats.ctmp}°C${Number(stats.cpwr || -1) > 0 ? ` • ${stats.cpwr}W` : ""}`
      : "CPU Temp: Run LibreHardwareMonitor for Tdie WMI sensor";

    $("#v-gpu-name").textContent = stats.gpun || "No NVIDIA GPU";
    setRing($("#ring-gpu"), hasGpu ? (stats.gpu ?? 0) : 0);
    $("#v-gpu-pct").textContent = hasGpu ? `${stats.gpu ?? 0}%` : "N/A";
    $("#v-gpu-temp").textContent = Number(stats.gtmp ?? -1) > 0 ? `${stats.gtmp}°C` : (hasGpu ? "N/A" : "Integrated");
    $("#v-gpu-pwr").textContent = Number(stats.gpmx || 0) > 0 ? `${stats.gpwr || 0} / ${stats.gpmx}W` : "Power N/A";
    $("#v-gpu-vram").textContent = hasGpu ? `${(stats.vram / 1024).toFixed(1)}/${(stats.vtot / 1024).toFixed(0)} GB` : "VRAM N/A";

    $("#v-ram-tot").textContent = `${Number(stats.rtot || 0).toFixed(1)} GB`;
    setRing($("#ring-ram"), stats.ram ?? 0);
    $("#v-ram-pct").textContent = `${stats.ram ?? 0}%`;
    $("#v-ram-use").textContent = `${Number(stats.ruse || 0).toFixed(1)} GB`;
    $("#v-ram-proc").textContent = stats.rproc || "-";
    $("#v-disk-free").textContent = `C: ${stats.dfre ?? 0} GB free`;

    $("#v-detail-title").textContent = hasGpu ? "GPU TELEMETRY & LOGICAL THREAD MATRIX" : "SYSTEM BARS & LOGICAL THREAD MATRIX";
    $("#v-thread-count").textContent = `${allCores.length} Threads`;

    const gpuBars = $("#gpu-bars");
    if (hasGpu) {
      const vramPct = Math.round(((stats.vram || 0) / Math.max(1, stats.vtot)) * 100);
      const pwrPct = stats.gpmx > 0 ? Math.min(100, Math.round(((stats.gpwr || 0) / stats.gpmx) * 100)) : 0;
      gpuBars.innerHTML = `
        <div class="gbar-row"><div class="gbar-lbl"><span>VRAM (${stats.vram} / ${stats.vtot} MB)</span><span>${vramPct}%</span></div><div class="gbar-track"><div class="gbar-fill" style="width:${vramPct}%;background:var(--emerald);"></div></div></div>
        <div class="gbar-row"><div class="gbar-lbl"><span>Board Power (${stats.gpwr || 0}W / ${stats.gpmx || 0}W)</span><span>${pwrPct}%</span></div><div class="gbar-track"><div class="gbar-fill" style="width:${pwrPct}%;background:var(--amber);"></div></div></div>
        <div class="gbar-row"><div class="gbar-lbl"><span>Clocks • Core ${stats.gclk || 0} MHz / Mem ${stats.gmck || 0} MHz</span><span>Fan ${stats.gfan || 0}%</span></div></div>
      `;
    } else {
      const diskPct = (stats.disks && stats.disks[0]) ? stats.disks[0].pct : 0;
      gpuBars.innerHTML = `
        <div class="gbar-row"><div class="gbar-lbl"><span>RAM Allocation (${Number(stats.ruse || 0).toFixed(1)} / ${Number(stats.rtot || 0).toFixed(1)} GB)</span><span>${stats.ram ?? 0}%</span></div><div class="gbar-track"><div class="gbar-fill" style="width:${stats.ram ?? 0}%;background:var(--violet);"></div></div></div>
        <div class="gbar-row"><div class="gbar-lbl"><span>System Drive Usage</span><span>${diskPct}%</span></div><div class="gbar-track"><div class="gbar-fill" style="width:${diskPct}%;background:var(--cyan);"></div></div></div>
      `;
    }

    $("#core-matrix").innerHTML = allCores.map((val, idx) => {
      const col = val >= 80 ? "#ff3a62" : val >= 50 ? "#ffb62e" : "#00f0da";
      const pct = Math.max(0, Math.min(100, Number(val) || 0));
      return `<div class="core-cell" style="border-color:${col}44;"><div style="color:${col};font-weight:700;">C${idx}</div><div>${pct}%</div><div class="core-bar"><div class="core-fill" style="width:${pct}%;background:${col};"></div></div></div>`;
    }).join("");

    $("#net-strip").textContent = `NET DOWN: ${Number(stats.ndn || 0).toFixed(2)} Mbps // NET UP: ${Number(stats.nup || 0).toFixed(2)} Mbps`;

    const renderProcs = (items, unit, color, scaleMax) => {
      if (!items || !items.length) return `<div class="muted-text">No active consumers</div>`;
      return items.map((p) => {
        const w = Math.max(4, Math.min(100, Math.round((Number(p.val || 0) / scaleMax) * 100)));
        return `
          <div class="proc-row">
            <div class="proc-top"><span class="truncate">${esc(p.name)}</span><span class="mono">${Number(p.val || 0).toFixed(1)} ${unit}</span></div>
            <div class="proc-bar"><div style="width:${w}%;background:${color};"></div></div>
          </div>
        `;
      }).join("");
    };
    $("#top-cpu-procs").innerHTML = renderProcs(stats.top_cpu_list, "%", "var(--cyan)", 50);
    $("#top-ram-procs").innerHTML = renderProcs(stats.top_ram_list, "GB", "var(--violet)", Math.max(1, Number(stats.rtot || 16) * 0.4));

    $("#disk-io-rate").textContent = `R: ${Number(stats.disk_r_mbps || 0).toFixed(1)} MB/s // W: ${Number(stats.disk_w_mbps || 0).toFixed(1)} MB/s`;
    $("#v-disks-list").innerHTML = (stats.disks || []).map((d) => `
      <div class="disk-row">
        <div class="proc-top"><strong>${esc(d.mount)}</strong><span class="mono">${d.free_gb} GB free / ${d.total_gb} GB (${d.pct}%)</span></div>
        <div class="proc-bar"><div style="width:${d.pct}%;background:${d.pct > 85 ? "var(--crimson)" : "var(--emerald)"};"></div></div>
      </div>
    `).join("") || `<div class="muted-text">No fixed volumes detected.</div>`;

    // Profiles live active window banner & twin quick specs
    $("#active-exe-now").textContent = stats.active_exe || "-";
    $("#active-title-now").textContent = stats.active_title ? `— ${stats.active_title}` : "";
    $("#twin-page-name").textContent = config?.pages?.[activePage]?.name || "MAIN";
    $("#twin-song").textContent = stats.song || "No active media";
    $("#twin-disk").textContent = `${stats.dfre ?? "--"} GB`;

    renderTelemetryChart();
  }

  /* ---------- AUTO PROFILES ---------- */

  function renderRules() {
    if (!config) return;
    $("#f-auto-switch").checked = !!config.auto_profile_switch;
    $("#f-auto-home").checked = !!config.auto_return_home;
    const pages = config.pages || [];
    const list = $("#rules-list");
    const rules = config.app_rules || [];

    const toolbar = `
      <div class="rules-toolbar">
        <span class="muted-text mono">${rules.length} Rule${rules.length === 1 ? "" : "s"} Configured</span>
        <button type="button" class="btn btn-accent btn-xs" id="btn-add-blank-rule">+ Add Blank Rule</button>
      </div>
    `;

    const headers = rules.length ? `
      <div class="rules-head">
        <span>Process Exe</span>
        <span>Window Title Substring</span>
        <span>Target Page</span>
        <span>Active</span>
        <span></span>
      </div>
    ` : "";

    const rows = rules.length ? rules.map((r, idx) => `
      <div class="rule-row" data-ridx="${idx}">
        <input type="text" class="rule-proc" placeholder="exe (e.g. Code.exe)" value="${esc(r.process || "")}" />
        <input type="text" class="rule-title" placeholder="title contains (optional)" value="${esc(r.title_contains || "")}" />
        <select class="rule-pg">
          ${pages.map((p, pi) => `<option value="${pi}" ${Number(r.page_index) === pi ? "selected" : ""}>${pi + 1}. ${esc(p.name)}</option>`).join("")}
        </select>
        <label class="toggle-check" style="margin-top:0;"><input type="checkbox" class="rule-en" ${r.enabled ? "checked" : ""} /> <span>On</span></label>
        <button type="button" class="btn btn-danger btn-xs rule-del">Del</button>
      </div>
    `).join("") : `<div class="muted-text">No rules defined. Focus a window and click + Bind Active Window, or + Add Blank Rule above.</div>`;

    list.innerHTML = toolbar + headers + rows;

    const addBlank = $("#btn-add-blank-rule");
    if (addBlank) {
      addBlank.addEventListener("click", () => {
        config.app_rules.push({ process: "", title_contains: "", page_index: activePage, enabled: true });
        renderRules();
        const inputs = list.querySelectorAll(".rule-proc");
        if (inputs.length) inputs[inputs.length - 1].focus();
      });
    }

    list.querySelectorAll(".rule-row").forEach((row) => {
      const idx = Number(row.dataset.ridx);
      const syncRule = () => {
        config.app_rules[idx] = {
          process: row.querySelector(".rule-proc").value.trim(),
          title_contains: row.querySelector(".rule-title").value.trim(),
          page_index: Number(row.querySelector(".rule-pg").value),
          enabled: !!row.querySelector(".rule-en").checked,
        };
        persistConfig("Profile rule saved");
      };
      row.querySelector(".rule-proc").addEventListener("change", syncRule);
      row.querySelector(".rule-title").addEventListener("change", syncRule);
      row.querySelector(".rule-pg").addEventListener("change", syncRule);
      row.querySelector(".rule-en").addEventListener("change", syncRule);
      row.querySelector(".rule-del").addEventListener("click", () => {
        config.app_rules.splice(idx, 1);
        renderRules();
        persistConfig("Rule deleted");
      });
    });
  }

  /* ---------- DEVICE & SERIAL DIAGNOSTICS ---------- */

  function renderDeviceForm(ports) {
    if (!config) return;
    const ds = config.device_settings || {};
    $("#cfg-bl").value = ds.bl ?? 230;
    $("#cfg-bl-val").textContent = `${Math.round(((ds.bl ?? 230) / 255) * 100)}%`;
    $("#cfg-rot").value = String(ds.rot ?? 1);
    $("#cfg-led").value = String(ds.led ?? 0);
    $("#cfg-slp").value = String(ds.slp ?? 300);
    $("#cfg-vpanel").value = config.virtual_panel || "ili9341";
    $("#cfg-sfx").checked = ds.sfx !== false;
    $("#cfg-inv").checked = !!ds.inv;
    $("#cfg-bgr").checked = !!ds.bgr;

    const sel = $("#cfg-port");
    if (ports && ports.length && sel) {
      const cur = config.serial_port || "COM11";
      sel.innerHTML = ports.map((p) => {
        const dev = p.device || p.port || "";
        const desc = p.description || p.desc || "Serial";
        return `<option value="${esc(dev)}" ${dev === cur ? "selected" : ""}>${esc(dev)} — ${esc(desc)}</option>`;
      }).join("");
      if (!ports.some((p) => (p.device || p.port) === cur)) {
        sel.innerHTML += `<option value="${esc(cur)}" selected>${esc(cur)} (Saved)</option>`;
      }
    }
    setCfgDirty(false);
  }

  function renderSerialStatus(serial, bins, vcyd) {
    if (serial) latestSerial = serial;
    if (bins) latestBins = bins;
    if (vcyd) latestVCyd = vcyd;

    const dot = $("#serial-dot");
    const lbl = $("#serial-label");
    if (latestSerial.hydro_handshake) {
      dot.className = "dot linked";
      lbl.textContent = `${latestSerial.port || "COM11"} LIVE`;
    } else if (latestSerial.connected) {
      dot.className = "dot";
      lbl.textContent = `${latestSerial.port || "COM11"} OPEN`;
    } else if (latestVCyd && latestVCyd.initialized) {
      dot.className = latestSerial.paused ? "dot" : "dot emu";
      lbl.textContent = latestSerial.paused ? "EMU ACTIVE (USB FREE)" : "EMU ACTIVE (NO USB)";
    } else if (latestSerial.paused) {
      dot.className = "dot error";
      lbl.textContent = `${latestSerial.port || "COM11"} RELEASED`;
    } else {
      dot.className = "dot error";
      lbl.textContent = `${latestSerial.port || "COM11"} OFFLINE`;
    }

    const diag = $("#serial-diag-box");
    if (diag) {
      diag.innerHTML = [
        `Port: <strong>${esc(latestSerial.port || "COM11")}</strong> @ ${latestSerial.baud || 115200} bps | Connected: ${!!latestSerial.connected} | Paused: ${!!latestSerial.paused}`,
        `HYDRO Link: ${!!latestSerial.hydro_handshake} | TX: ${latestSerial.tx_packets || 0} | RX: ${latestSerial.rx_lines || 0}`,
        `Status: ${esc(latestSerial.diagnostic || "Ready")}${latestSerial.last_error ? ` (${esc(latestSerial.last_error)})` : ""}`,
        `Virtual CYD C++ DLL: ${latestVCyd.panel_type ? `${latestVCyd.panel_type.toUpperCase()} active • Screen ${latestVCyd.screen}` : "Running"}`,
      ].join("<br>");
    }

    const binBox = $("#fw-bins");
    if (binBox && latestBins) {
      const b1 = latestBins.e32r28t_ili9341 || {};
      const b2 = latestBins.e32r28t_st7789 || {};
      binBox.innerHTML =
        `e32r28t_ili9341: ${b1.exists ? `READY (${b1.size_kb} KB)` : "NOT BUILT"}<br>` +
        `e32r28t_st7789 : ${b2.exists ? `READY (${b2.size_kb} KB)` : "NOT BUILT"}`;
    }

    const logBox = $("#serial-log");
    if (logBox) {
      const serLogs = latestSerial.recent_logs || [];
      if (serLogs.length) {
        logBox.textContent = serLogs.join("\n");
      } else if (latestVCyd.serial_logs && latestVCyd.serial_logs.length) {
        logBox.textContent = latestVCyd.serial_logs.map((l) => `[VCYD ${l.dir}] ${l.line}`).join("\n");
      }
    }
  }

  /* ---------- LIVE VIRTUAL CYD MIRROR & TOUCH ---------- */

  function updateVirtualTwinHud(st) {
    if (!st) return;
    latestVCyd = st;
    const gCorr = (c) => Math.min(255, Math.round(Math.pow(Math.max(0, Number(c) || 0) / 255, 0.55) * 255));
    const led = st.led || { r: 0, g: 0, b: 0 };
    const lr = gCorr(led.r), lg = gCorr(led.g), lb = gCorr(led.b);
    const ledEl = $("#twin-led");
    if (ledEl) {
      ledEl.style.background = `rgb(${lr}, ${lg}, ${lb})`;
      ledEl.style.boxShadow = (led.r + led.g + led.b > 12) ? `0 0 12px rgb(${lr}, ${lg}, ${lb})` : "none";
    }
    const scrLower = String(st.screen || "deck").toLowerCase();
    $$("[data-cyd-scr]").forEach((b) => b.classList.toggle("active", b.dataset.cydScr === scrLower));
    const vsubRow = $("#vitals-sub-row");
    if (vsubRow) vsubRow.style.display = scrLower === "vitals" ? "flex" : "none";
    if (st.vitals_sub !== undefined) {
      const vsubNames = ["overview", "cpu", "gpu", "ram"];
      const curVsub = typeof st.vitals_sub === "number"
        ? (vsubNames[st.vitals_sub] || "overview")
        : String(st.vitals_sub || "overview").toLowerCase();
      $$("[data-vsub]").forEach((b) => b.classList.toggle("active", b.dataset.vsub === curVsub));
    }

    const pageIdx = Number(st.page ?? activePage);
    $("#twin-page-name").textContent = config?.pages?.[pageIdx]?.name || `PAGE ${pageIdx + 1}`;
  }

  function playStudioBeeps(events) {
    if (!events || !events.length || !$("#cfg-sfx")?.checked) return;
    try {
      if (!studioAudioCtx) studioAudioCtx = new (window.AudioContext || window.webkitAudioContext)();
      if (studioAudioCtx.state === "suspended") studioAudioCtx.resume().catch(() => {});
      let cursor = studioAudioCtx.currentTime;
      for (const ev of events) {
        const durSec = Math.max(0.02, (ev.dur || 60) / 1000.0);
        if (ev.freq && ev.freq > 20) {
          const osc = studioAudioCtx.createOscillator();
          const gain = studioAudioCtx.createGain();
          osc.type = "triangle";
          osc.frequency.value = ev.freq;
          gain.gain.setValueAtTime(0.04, cursor);
          gain.gain.exponentialRampToValueAtTime(0.001, cursor + durSec);
          osc.connect(gain);
          gain.connect(studioAudioCtx.destination);
          osc.start(cursor);
          osc.stop(cursor + durSec);
        }
        cursor += durSec + 0.015;
      }
    } catch (_) {}
  }

  function initVirtualTwin() {
    const canvas = $("#twin-canvas");
    if (!canvas) return;
    vcydCanvasCtx = canvas.getContext("2d");
    vcydImageData = vcydCanvasCtx.createImageData(320, 240);

    // Bootstrap immediate crisp frame via HTTP PNG
    const bootImg = new Image();
    bootImg.onload = () => {
      if (vcydCanvasCtx) vcydCanvasCtx.drawImage(bootImg, 0, 0, 320, 240);
    };
    bootImg.src = `/api/virtual/frame.png?ts=${Date.now()}`;

    const emitTouch = (down, ev) => {
      const rect = canvas.getBoundingClientRect();
      const x = Math.max(0, Math.min(319, Math.round(((ev.clientX - rect.left) / rect.width) * 320)));
      const y = Math.max(0, Math.min(239, Math.round(((ev.clientY - rect.top) / rect.height) * 240)));
      if (vcydWs && vcydWs.readyState === WebSocket.OPEN) {
        vcydWs.send(JSON.stringify({ type: "touch", down, x, y }));
      } else {
        fetch("/api/virtual/touch", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ down, x, y }),
        }).catch(() => {});
      }
    };

    canvas.addEventListener("pointerdown", (e) => {
      canvas.setPointerCapture(e.pointerId);
      vcydPointerDown = true;
      emitTouch(true, e);
    });
    canvas.addEventListener("pointermove", (e) => {
      if (!vcydPointerDown) return;
      emitTouch(true, e);
    });
    canvas.addEventListener("pointerup", (e) => {
      if (!vcydPointerDown) return;
      vcydPointerDown = false;
      emitTouch(false, e);
    });
    canvas.addEventListener("pointercancel", (e) => {
      if (!vcydPointerDown) return;
      vcydPointerDown = false;
      emitTouch(false, e);
    });

    const bootBtn = $("#btn-cyd-boot");
    if (bootBtn) {
      bootBtn.addEventListener("pointerdown", () => {
        if (vcydWs && vcydWs.readyState === WebSocket.OPEN) vcydWs.send(JSON.stringify({ type: "boot", down: true }));
        else apiJson("/api/virtual/boot", { method: "POST", body: JSON.stringify({ down: true }) }).catch(() => {});
      });
      const releaseBoot = () => {
        if (vcydWs && vcydWs.readyState === WebSocket.OPEN) vcydWs.send(JSON.stringify({ type: "boot", down: false }));
        else apiJson("/api/virtual/boot", { method: "POST", body: JSON.stringify({ down: false }) }).catch(() => {});
      };
      bootBtn.addEventListener("pointerup", releaseBoot);
      bootBtn.addEventListener("pointercancel", releaseBoot);
    }

    const connectVcydWs = () => {
      const proto = location.protocol === "https:" ? "wss:" : "ws:";
      vcydWs = new WebSocket(`${proto}//${location.host}/ws/virtual-cyd`);
      vcydWs.binaryType = "arraybuffer";
      vcydWs.onopen = () => {
        $("#twin-fps-lbl").textContent = "LIVE C++";
      };
      vcydWs.onclose = () => {
        $("#twin-fps-lbl").textContent = "RECONN...";
        setTimeout(connectVcydWs, 1200);
      };
      vcydWs.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) {
          const bytes = new Uint8ClampedArray(ev.data);
          if (bytes.length === 320 * 240 * 4) {
            vcydImageData.data.set(bytes);
            vcydCanvasCtx.putImageData(vcydImageData, 0, 0);
            vcydFrames += 1;
            const now = performance.now();
            if (now - vcydLastFpsTs >= 1000) {
              const fps = Math.round((vcydFrames * 1000) / (now - vcydLastFpsTs));
              $("#twin-fps-lbl").textContent = `C++ ${fps} FPS`;
              vcydFrames = 0;
              vcydLastFpsTs = now;
            }
          }
        } else if (typeof ev.data === "string") {
          try {
            const msg = JSON.parse(ev.data);
            if (msg.type === "state") updateVirtualTwinHud(msg.state);
            if (msg.type === "audio") playStudioBeeps(msg.events);
          } catch (_) {}
        }
      };
    };
    connectVcydWs();
  }

  /* ---------- RENDER ALL & INITIAL WIRING ---------- */

  function renderAll(ports = []) {
    renderPageTabs();
    renderDeckGrid();
    loadInspector();
    renderRules();
    renderDeviceForm(ports);
  }

  function wireStaticEvents() {
    $$(".nav-item").forEach((b) => {
      b.addEventListener("click", () => switchTab(b.dataset.tab));
    });
    $("#serial-pill").addEventListener("click", () => switchTab("device"));

    // Global Hotkey Recorder
    window.addEventListener("keydown", (ev) => {
      if (!recordingTarget) return;
      ev.preventDefault();
      ev.stopPropagation();
      if (ev.code === "Escape" && !ev.ctrlKey && !ev.altKey && !ev.metaKey && !ev.shiftKey) {
        cancelKeyRecording();
        return;
      }
      const combo = buildComboFromKeyEvent(ev);
      if (!combo) return;
      const target = recordingTarget;
      cancelKeyRecording();
      if (target.inputEl) target.inputEl.value = combo;
      if (target.onDone) target.onDone(combo);
      setDirty(true);
    }, true);

    // Topbar
    $("#btn-sync").addEventListener("click", async () => {
      await flushDirtyInspector();
      const res = await apiJson("/api/device/sync", { method: "POST" });
      renderSerialStatus(res.serial, null, res.virtual_cyd);
      showToast("Synced config & live telemetry to CYD");
    });
    $("#btn-export").addEventListener("click", () => {
      window.location.href = "/api/config/export";
    });
    $("#btn-import").addEventListener("click", () => $("#import-file").click());
    $("#import-file").addEventListener("change", async (e) => {
      const f = e.target.files?.[0];
      if (!f) return;
      try {
        const raw = await f.text();
        const res = await apiJson("/api/config/import", {
          method: "POST",
          body: JSON.stringify({ raw_json: raw }),
        });
        config = res.config;
        activePage = 0;
        selectedTile = 0;
        renderAll();
        showToast("Imported deck profile JSON");
      } catch (err) {
        showToast(err.message);
      }
      e.target.value = "";
    });

    // Audio Rail
    $("#rail-vol-slider").addEventListener("input", (e) => {
      $("#rail-vol").textContent = `${e.target.value}%`;
      apiJson("/api/volume", { method: "POST", body: JSON.stringify({ pct: Number(e.target.value) }) }).catch(() => {});
    });
    $("#rail-mic-btn").addEventListener("click", async () => {
      const res = await apiJson("/api/mic/toggle", { method: "POST" });
      showToast(res.mic_muted ? "Microphone Muted" : "Microphone Live");
    });
    $("#rail-play-btn").addEventListener("click", () => {
      apiJson("/api/media/play_pause", { method: "POST" }).catch(() => {});
    });

    // Page Controls
    $("#btn-templates").addEventListener("click", async () => {
      await flushDirtyInspector();
      openTemplatesModal();
    });
    $("#tpl-close").addEventListener("click", () => { $("#templates-modal").style.display = "none"; });
    $("#modal-cancel").addEventListener("click", closeTextModal);
    $("#modal-confirm").addEventListener("click", async () => {
      const fn = activeModalConfirm;
      const val = $("#modal-input").value.trim();
      closeTextModal();
      if (fn) await fn(val);
    });
    $("#modal-input").addEventListener("keydown", (e) => {
      if (e.key === "Enter") $("#modal-confirm").click();
      if (e.key === "Escape") closeTextModal();
    });

    $("#btn-rename-page").addEventListener("click", () => {
      const curName = config.pages[activePage]?.name || "";
      openTextModal({
        title: "RENAME PAGE",
        label: `Name for Page ${activePage + 1}`,
        value: curName,
        placeholder: "e.g. STREAMING",
        onConfirm: async (val) => {
          if (!val) return;
          config.pages[activePage].name = val.slice(0, 12).toUpperCase();
          await persistConfig("Page renamed");
          renderPageTabs();
          renderRules();
        },
      });
    });

    $("#btn-add-page").addEventListener("click", () => {
      if (config.pages.length >= 6) return showToast("Maximum 6 pages supported on CYD");
      openTextModal({
        title: "ADD NEW PAGE",
        label: "New Page Name",
        value: `PAGE ${config.pages.length + 1}`,
        placeholder: "e.g. EDITING",
        onConfirm: async (val) => {
          const name = (val || `PAGE ${config.pages.length + 1}`).slice(0, 12).toUpperCase();
          const tiles = Array.from({ length: 6 }, (_, i) => ({
            label: `KEY ${i + 1}`,
            sub: "EMPTY",
            icon: "bolt",
            col: "#00F0DA",
            tog: false,
            on: false,
            jmp: -1,
            action_type: "hotkey",
            action_payload: "",
            hold_action_type: "",
            hold_action_payload: "",
          }));
          config.pages.push({ name, tiles });
          activePage = config.pages.length - 1;
          selectedTile = 0;
          await persistConfig(`Added page ${name}`);
          renderAll();
        },
      });
    });

    $("#btn-del-page").addEventListener("click", () => {
      if (config.pages.length <= 1) return showToast("Cannot delete the last page");
      const curName = config.pages[activePage]?.name || `Page ${activePage + 1}`;
      openConfirmModal({
        title: "DELETE DECK PAGE",
        message: `Delete "${curName}" and remap remaining folder jumps?`,
        confirmText: "Delete Page",
        onConfirm: async () => {
          try {
            const res = await apiJson(`/api/pages/${activePage}/delete`, { method: "POST" });
            config = res.config;
            activePage = res.active_page ?? 0;
            selectedTile = 0;
            setDirty(false);
            renderAll();
            showToast("Page deleted & links remapped");
          } catch (err) {
            showToast(err.message);
          }
        },
      });
    });

    // Inspector Listeners
    ["#f-label", "#f-sub", "#f-off-label", "#f-on-label"].forEach((id) => {
      $(id).addEventListener("input", () => setDirty(true));
    });
    $("#f-color").addEventListener("input", (e) => {
      renderPaletteSwatches(e.target.value);
      setDirty(true);
    });
    $("#f-tog").addEventListener("change", (e) => {
      $("#ab-labels-row").style.display = e.target.checked ? "grid" : "none";
      $("#state-b-action-wrap").style.display = e.target.checked ? "flex" : "none";
      setDirty(true);
    });
    $("#f-sb-type").addEventListener("change", (e) => {
      cancelKeyRecording();
      buildSinglePayloadEditor($("#state-b-payload-area"), e.target.value, "", "state_b");
      setDirty(true);
    });
    $("#f-hrepeat").addEventListener("change", () => setDirty(true));
    $("#f-atype").addEventListener("change", (e) => {
      cancelKeyRecording();
      buildSinglePayloadEditor($("#payload-area"), e.target.value, "", "primary");
      setDirty(true);
    });
    $("#f-htype").addEventListener("change", (e) => {
      cancelKeyRecording();
      buildSinglePayloadEditor($("#hold-payload-area"), e.target.value, "", "hold");
      setDirty(true);
    });
    $("#btn-save-tile").addEventListener("click", async () => {
      collectInspectorIntoTile();
      await persistConfig("Key saved & synced to CYD");
      renderDeckGrid();
    });
    $("#btn-test-tile").addEventListener("click", async () => {
      await flushDirtyInspector();
      renderDeckGrid();
      testTile(activePage, selectedTile, false);
    });
    $("#btn-test-hold").addEventListener("click", async () => {
      await flushDirtyInspector();
      renderDeckGrid();
      testTile(activePage, selectedTile, true);
    });

    // Chart mode toggle
    $$("[data-cmode]").forEach((btn) => {
      btn.addEventListener("click", () => {
        chartMode = btn.dataset.cmode;
        $$("[data-cmode]").forEach((b) => b.classList.toggle("active", b.dataset.cmode === chartMode));
        renderTelemetryChart();
      });
    });

    // Profiles
    $("#f-auto-switch").addEventListener("change", (e) => {
      config.auto_profile_switch = !!e.target.checked;
      persistConfig(config.auto_profile_switch ? "Auto-switch enabled" : "Auto-switch paused");
    });
    $("#f-auto-home").addEventListener("change", (e) => {
      config.auto_return_home = !!e.target.checked;
      persistConfig(config.auto_return_home ? "Return to Page 1 enabled" : "Return to Page 1 disabled");
    });
    $("#btn-bind-active").addEventListener("click", () => {
      const exe = (latestStats.active_exe || "").trim();
      if (!exe || exe === "-") return showToast("No active desktop app detected yet");
      config.app_rules.push({ process: exe, title_contains: "", page_index: activePage, enabled: true });
      renderRules();
      persistConfig(`Bound ${exe} -> ${config.pages[activePage].name}`);
    });

    // Device & Flash
    $("#cfg-bl").addEventListener("input", (e) => {
      $("#cfg-bl-val").textContent = `${Math.round((Number(e.target.value) / 255) * 100)}%`;
      setCfgDirty(true);
    });
    ["#cfg-rot", "#cfg-led", "#cfg-slp", "#cfg-port", "#cfg-sfx", "#cfg-inv", "#cfg-bgr"].forEach((sel) => {
      $(sel)?.addEventListener("change", () => setCfgDirty(true));
    });
    $("#cfg-vpanel").addEventListener("change", async (e) => {
      try {
        await apiJson("/api/virtual/panel", { method: "POST", body: JSON.stringify({ panel: e.target.value }) });
        config.virtual_panel = e.target.value;
        showToast(`Virtual CYD panel -> ${e.target.value.toUpperCase()}`);
      } catch (err) {
        showToast(err.message);
      }
    });
    $("#btn-save-cfg").addEventListener("click", async () => {
      await flushDirtyInspector();
      config.device_settings = {
        bl: Number($("#cfg-bl").value),
        rot: Number($("#cfg-rot").value),
        led: Number($("#cfg-led").value),
        slp: Number($("#cfg-slp").value),
        sfx: !!$("#cfg-sfx").checked,
        inv: !!$("#cfg-inv").checked,
        bgr: !!$("#cfg-bgr").checked,
      };
      if ($("#cfg-port")?.value) config.serial_port = $("#cfg-port").value;
      config.virtual_panel = $("#cfg-vpanel").value;
      await persistConfig("Hardware config pushed to CYD");
    });
    $("#btn-reset-cfg").addEventListener("click", () => {
      openConfirmModal({
        title: "RESET FACTORY DEFAULTS",
        message: "Restore all deck pages, auto-switch rules, and device settings to factory defaults?",
        confirmText: "Restore Defaults",
        onConfirm: async () => {
          setDirty(false);
          const res = await apiJson("/api/config/reset", { method: "POST" });
          config = res.config;
          activePage = 0;
          selectedTile = 0;
          renderAll();
          showToast("Factory defaults restored");
        },
      });
    });
    $("#btn-connect-port").addEventListener("click", async () => {
      const port = $("#cfg-port").value;
      const res = await apiJson("/api/serial/connect", { method: "POST", body: JSON.stringify({ port, baud: 115200 }) });
      renderSerialStatus(res.serial);
      showToast(`Connecting to ${port}...`);
    });
    $("#btn-disconnect-port").addEventListener("click", async () => {
      const res = await apiJson("/api/serial/disconnect", { method: "POST" });
      renderSerialStatus(res.serial);
      showToast("COM port released cleanly");
    });
    $("#btn-export-sd").addEventListener("click", async () => {
      const res = await apiJson("/api/firmware/export_sd", { method: "POST" });
      showToast(`Exported ${(res.files || []).length} files to firmware_dist/`);
    });

    // Twin Hardware Buttons & Screens
    $("#btn-twin-zoom").addEventListener("click", () => {
      twinZoom = !twinZoom;
      $("#twin-screen-wrap").classList.toggle("zoom", twinZoom);
      $("#btn-twin-zoom").textContent = twinZoom ? "1.1x" : "1x";
      $("#btn-twin-zoom").classList.toggle("active", twinZoom);
    });
    $("#btn-popout-vcyd").addEventListener("click", () => {
      window.open("/virtual-cyd", "hydro_vcyd", "width=980,height=700");
    });
    $("#btn-cyd-cal").addEventListener("click", () => {
      apiJson("/api/cyd/calibrate", { method: "POST" }).then(() => showToast("Calibration started on CYD")).catch(() => {});
    });
    $("#btn-cyd-sleep").addEventListener("click", () => {
      apiJson("/api/cyd/sleep", { method: "POST" }).then(() => showToast("CYD screen sleeping (tap to wake)")).catch(() => {});
    });
    $("#btn-cyd-reset").addEventListener("click", async () => {
      const res = await apiJson("/api/virtual/reset", { method: "POST" });
      updateVirtualTwinHud(res.state);
      showToast("Virtual CYD MCU rebooted");
    });

    $$("[data-cyd-scr]").forEach((b) => {
      b.addEventListener("click", () => {
        apiJson("/api/cyd/touch_sim", {
          method: "POST",
          body: JSON.stringify({ screen: b.dataset.cydScr, page: activePage }),
        }).catch(() => {});
      });
    });
    $$("[data-vsub]").forEach((b) => {
      b.addEventListener("click", async () => {
        $$("[data-vsub]").forEach((btn) => btn.classList.toggle("active", btn === b));
        await apiJson("/api/cyd/touch_sim", {
          method: "POST",
          body: JSON.stringify({ screen: "vitals", page: activePage, vitals_sub: b.dataset.vsub }),
        }).catch(() => {});
      });
    });

    window.addEventListener("resize", () => {
      if ($("#tab-vitals")?.classList.contains("active")) renderTelemetryChart();
    });
  }

  function connectStudioWs() {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === "tick") {
          if (msg.history) telemHistory = msg.history;
          if (msg.stats) renderStats(msg.stats);
          if (msg.serial || msg.virtual_cyd) renderSerialStatus(msg.serial, null, msg.virtual_cyd);
          if (msg.virtual_cyd) updateVirtualTwinHud(msg.virtual_cyd);
          if (msg.macro_history) renderMacroLog(msg.macro_history);
        } else if (msg.type === "cfg_update" && msg.device_settings && config) {
          config.device_settings = { ...(config.device_settings || {}), ...msg.device_settings };
          renderDeviceForm();
        }
      } catch (_) {}
    };
    ws.onclose = () => setTimeout(connectStudioWs, 1500);
  }

  async function init() {
    wireStaticEvents();
    initVirtualTwin();

    const params = new URLSearchParams(window.location.search);
    const reqTab = params.get("tab");
    if (reqTab && ["deck", "vitals", "profiles", "device"].includes(reqTab)) {
      switchTab(reqTab);
    }

    const st = await apiJson("/api/state");
    config = st.config;
    telemHistory = st.history || [];
    activePage = st.runtime?.active_page ?? 0;

    renderAll(st.ports || []);
    renderStats(st.stats);
    renderSerialStatus(st.serial, st.firmware_bins, st.virtual_cyd);
    updateVirtualTwinHud(st.virtual_cyd);
    renderMacroLog(st.macro_history || []);
    connectStudioWs();
  }

  document.addEventListener("DOMContentLoaded", init);
})();
