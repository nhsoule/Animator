//----- Viewer page -----||

let module = null;
let booted = false;
let seenRev = null;

const errorEl = document.getElementById("error");
const busyEl = document.getElementById("busy");
const playBtn = document.getElementById("play");
const placeBtn = document.getElementById("place");
const scaleInput = document.getElementById("scale");
const scaleVal = document.getElementById("scale-val");
const slidersEl = document.getElementById("sliders");
const canvas = document.getElementById("canvas");

const TAP_SLOP = 8;
const DOUBLE_TAP_MS = 300;

function showError(text) {
  errorEl.style.display = "block";
  errorEl.textContent = text || "";
}

function hideError() {
  errorEl.style.display = "none";
  errorEl.textContent = "";
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error("failed to load " + src));
    document.body.appendChild(s);
  });
}

function formatNum(v) {
  if (!Number.isFinite(v)) return "0.00";
  return v.toFixed(2);
}

function clampThumb(value, min, max) {
  const lo = Math.min(min, max);
  const hi = Math.max(min, max);
  return Math.min(hi, Math.max(lo, value));
}

function saveUi() {
  if (!module) return;
  const saved = {};
  const n = module._anim_slider_count();
  for (let i = 0; i < n; i++) {
    saved[module.UTF8ToString(module._anim_slider_name(i))] = module._anim_slider_value(i);
  }
  sessionStorage.setItem("animator-sliders", JSON.stringify(saved));
  sessionStorage.setItem("animator-scale", String(module._anim_get_time_scale()));
  sessionStorage.setItem("animator-play", module._anim_get_playing() ? "1" : "0");
  sessionStorage.setItem("animator-place", module._anim_get_place() ? "1" : "0");
}

function restoreUi() {
  if (!module) return;
  try {
    const raw = sessionStorage.getItem("animator-sliders");
    if (raw) {
      const saved = JSON.parse(raw);
      const n = module._anim_slider_count();
      for (let i = 0; i < n; i++) {
        const name = module.UTF8ToString(module._anim_slider_name(i));
        if (Object.prototype.hasOwnProperty.call(saved, name)) module._anim_slider_set(i, saved[name]);
      }
    }
  } catch (e) { /* ignore a bad saved blob */ }
  const scale = sessionStorage.getItem("animator-scale");
  if (scale !== null) {
    const v = parseFloat(scale);
    if (Number.isFinite(v)) module._anim_set_time_scale(v);
  }
  const play = sessionStorage.getItem("animator-play");
  if (play !== null) module._anim_set_playing(play === "1" ? 1 : 0);
  const place = sessionStorage.getItem("animator-place");
  if (place !== null) module._anim_set_place(place === "1" ? 1 : 0);
}

//----- Typed slider values -----||
// Click the number to type. Enter or blur commits, Escape cancels.
// Typed values may sit outside the slider's range; only the thumb is clamped.

function bindNumInput(input, getValue, setValue) {
  let start = "";
  let editing = false;
  let cancelled = false;
  const show = () => {
    if (!editing) input.value = formatNum(getValue());
  };
  input.addEventListener("focus", () => {
    editing = true;
    cancelled = false;
    start = formatNum(getValue());
    input.value = start;
    input.select();
  });
  const commit = () => {
    if (!editing) return;
    const raw = input.value;
    editing = false;
    if (cancelled) {
      cancelled = false;
      input.value = start;
      return;
    }
    const v = parseFloat(raw);
    if (Number.isFinite(v)) setValue(v);
    else input.value = formatNum(getValue());
  };
  const cancel = () => {
    if (!editing) return;
    cancelled = true;
    input.blur();
  };
  input.addEventListener("blur", commit);
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      input.blur();
    } else if (e.key === "Escape") {
      e.preventDefault();
      cancel();
    }
  });
  return { show, isEditing: () => editing };
}

const scaleNum = bindNumInput(
  scaleVal,
  () => (module ? module._anim_get_time_scale() : parseFloat(scaleInput.value) || 1),
  (v) => {
    if (!module) return;
    module._anim_set_time_scale(v);
    syncChrome();
    saveUi();
  }
);

function syncChrome() {
  if (!module) return;
  const playing = module._anim_get_playing();
  playBtn.textContent = playing ? "Pause" : "Play";
  const placing = module._anim_get_place();
  placeBtn.classList.toggle("on", !!placing);
  const scale = module._anim_get_time_scale();
  scaleInput.value = String(clampThumb(scale, parseFloat(scaleInput.min), parseFloat(scaleInput.max)));
  scaleNum.show();
}

function buildSliders() {
  slidersEl.textContent = "";
  const n = module._anim_slider_count();
  for (let i = 0; i < n; i++) {
    const name = module.UTF8ToString(module._anim_slider_name(i));
    const min = module._anim_slider_min(i);
    const max = module._anim_slider_max(i);
    const value = module._anim_slider_value(i);
    const head = document.createElement("div");
    head.className = "slider-head";
    head.appendChild(document.createTextNode(name));
    const val = document.createElement("input");
    val.className = "slider-num";
    val.type = "text";
    val.inputMode = "decimal";
    val.setAttribute("enterkeyhint", "done");
    val.autocomplete = "off";
    val.spellcheck = false;
    val.setAttribute("aria-label", name + " value");
    val.value = formatNum(value);
    head.appendChild(val);
    const input = document.createElement("input");
    input.type = "range";
    input.min = String(min);
    input.max = String(max);
    input.step = "any";
    input.value = String(clampThumb(value, min, max));
    const num = bindNumInput(
      val,
      () => module._anim_slider_value(i),
      (v) => {
        module._anim_slider_set(i, v);
        const cur = module._anim_slider_value(i);
        input.value = String(clampThumb(cur, min, max));
        val.value = formatNum(cur);
        saveUi();
      }
    );
    input.addEventListener("input", () => {
      const v = parseFloat(input.value);
      module._anim_slider_set(i, v);
      num.show();
      saveUi();
    });
    slidersEl.appendChild(head);
    slidersEl.appendChild(input);
  }
}

//----- Camera input (Vert_Modeler Pointer Events) -----||
// One finger / left-drag: orbit after TAP_SLOP. Two fingers: midpoint pans,
// spread zooms. Right, middle, or Shift+left: pan. Wheel zooms. A still
// two-finger double-tap, double middle-click, or F recenters on the model.
// Place: one finger / left button drags on the ground; two fingers stay pan+pinch.
// After a pinch, the remaining finger is idle so it doesn't start an orbit or Place.

const pointers = new Map();
let gesture = null;
let lastTwoTap = 0;
let lastMiddleClick = 0;

function localPos(e) {
  const r = canvas.getBoundingClientRect();
  return { x: e.clientX - r.left, y: e.clientY - r.top };
}

function panDepth(g) {
  if (!module) return 1;
  const dist = module._anim_cam_dist();
  return Math.max(0.0005, g.depth - (g.dist0 - dist));
}

function endTwoFinger() {
  const g = gesture;
  gesture = null;
  if (!g || !g.twoTap) { lastTwoTap = 0; return; }
  const now = performance.now();
  if (now - lastTwoTap <= DOUBLE_TAP_MS + 100) {
    lastTwoTap = 0;
    if (module) module._anim_recenter();
  } else {
    lastTwoTap = now;
  }
}

canvas.addEventListener("pointerdown", (e) => {
  if (!module) return;
  canvas.setPointerCapture(e.pointerId);
  const p = localPos(e);
  pointers.set(e.pointerId, p);

  if (pointers.size === 2) {
    if (gesture && gesture.kind === "place") module._anim_place_end();
    const [a, b] = [...pointers.values()];
    const d = Math.hypot(a.x - b.x, a.y - b.y), mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    gesture = {
      kind: "pinch", lastDist: d, lastMid: [mx, my], startDist: d, startMid: [mx, my],
      t0: performance.now(), moved: false, depth: module._anim_grab_depth(mx, my),
      dist0: module._anim_cam_dist()
    };
    return;
  }
  if (pointers.size > 2) {
    if (gesture && gesture.kind === "pinch") gesture.moved = true;
    return;
  }

  if (e.pointerType === "mouse" && (e.button === 1 || e.button === 2 || (e.button === 0 && e.shiftKey))) {
    if (e.button === 1) {
      const now = performance.now();
      if (now - lastMiddleClick <= DOUBLE_TAP_MS) {
        lastMiddleClick = 0;
        module._anim_recenter();
      } else {
        lastMiddleClick = now;
      }
      e.preventDefault();
    }
    gesture = { kind: "pan", lastX: p.x, lastY: p.y, depth: module._anim_grab_depth(p.x, p.y), dist0: module._anim_cam_dist() };
    return;
  }

  if (module._anim_get_place() && (e.pointerType !== "mouse" || e.button === 0)) {
    gesture = { kind: "place", lastX: p.x, lastY: p.y };
    module._anim_place_begin(p.x, p.y);
    return;
  }

  gesture = { kind: "pending", startX: p.x, startY: p.y, lastX: p.x, lastY: p.y };
});

canvas.addEventListener("pointermove", (e) => {
  if (!module || !pointers.has(e.pointerId)) return;
  const p = localPos(e);
  pointers.set(e.pointerId, p);
  if (!gesture) return;

  if (gesture.kind === "pinch" && pointers.size >= 2) {
    const [a, b] = [...pointers.values()];
    const d = Math.hypot(a.x - b.x, a.y - b.y), mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    if (d > 0 && gesture.lastDist > 0) module._anim_zoom(gesture.lastDist / d);
    module._anim_pan(mx - gesture.lastMid[0], my - gesture.lastMid[1], panDepth(gesture));
    gesture.lastDist = d;
    gesture.lastMid = [mx, my];
    if (Math.abs(d - gesture.startDist) > TAP_SLOP ||
        Math.hypot(mx - gesture.startMid[0], my - gesture.startMid[1]) > TAP_SLOP) {
      gesture.moved = true;
    }
    return;
  }
  if (gesture.kind === "pan") {
    module._anim_pan(p.x - gesture.lastX, p.y - gesture.lastY, panDepth(gesture));
    gesture.lastX = p.x;
    gesture.lastY = p.y;
    return;
  }
  if (gesture.kind === "place") {
    module._anim_place_move(p.x, p.y);
    gesture.lastX = p.x;
    gesture.lastY = p.y;
    return;
  }
  if (gesture.kind === "pending" &&
      Math.hypot(p.x - gesture.startX, p.y - gesture.startY) > TAP_SLOP) {
    gesture.kind = "orbit";
  }
  if (gesture.kind === "orbit") {
    module._anim_orbit(p.x - gesture.lastX, p.y - gesture.lastY);
    gesture.lastX = p.x;
    gesture.lastY = p.y;
  }
});

function endPointer(e) {
  if (!pointers.has(e.pointerId)) return;
  pointers.delete(e.pointerId);
  if (!gesture) return;

  if (gesture.kind === "pinch") {
    // Lifting one of two fingers: don't let the remaining finger orbit or Place.
    const twoTap = !gesture.moved && performance.now() - gesture.t0 < 400;
    gesture = { kind: "idle", twoTap };
    if (!pointers.size) endTwoFinger();
    return;
  }
  if (pointers.size) return;
  if (gesture.kind === "idle") { endTwoFinger(); return; }
  if (gesture.kind === "place" && module) module._anim_place_end();
  gesture = null;
}

canvas.addEventListener("pointerup", endPointer);
canvas.addEventListener("pointercancel", endPointer);
canvas.addEventListener("wheel", (e) => {
  e.preventDefault();
  if (!module) return;
  module._anim_zoom(Math.exp(e.deltaY * 0.001));
}, { passive: false });

canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("auxclick", (e) => e.preventDefault());
document.addEventListener("gesturestart", (e) => e.preventDefault());
document.addEventListener("dblclick", (e) => e.preventDefault());
document.addEventListener("touchmove", (e) => { if (e.target === canvas) e.preventDefault(); }, { passive: false });

function camState() {
  if (!module) return null;
  return {
    yaw: module._anim_cam_yaw(),
    pitch: module._anim_cam_pitch(),
    dist: module._anim_cam_dist(),
    x: module._anim_cam_tx(),
    y: module._anim_cam_ty(),
    z: module._anim_cam_tz()
  };
}

window.__animDebug = {
  cam: camState,
  playing: () => (module ? module._anim_get_playing() : 0),
  pause: () => { playBtn.click(); }
};

async function boot(rev) {
  await loadScript("scene.js?rev=" + encodeURIComponent(rev));
  if (typeof createAnim !== "function") throw new Error("scene.js did not export createAnim");
  module = await createAnim({
    canvas,
    locateFile: (path) => (path.endsWith(".wasm") ? "scene.wasm" : path.replace(/^.*\//, "")) + "?rev=" + encodeURIComponent(rev),
  });
  module._anim_start();
  restoreUi();
  syncChrome();
  buildSliders();
  busyEl.textContent = "";
}

playBtn.addEventListener("click", () => {
  if (!module) return;
  module._anim_set_playing(module._anim_get_playing() ? 0 : 1);
  syncChrome();
  saveUi();
});

placeBtn.addEventListener("click", () => {
  if (!module) return;
  module._anim_set_place(module._anim_get_place() ? 0 : 1);
  syncChrome();
  saveUi();
});

scaleInput.addEventListener("input", () => {
  if (!module) return;
  module._anim_set_time_scale(parseFloat(scaleInput.value));
  syncChrome();
  saveUi();
});

window.addEventListener("keydown", (e) => {
  if (!module) return;
  if (e.target && (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA")) return;
  const mod = e.ctrlKey || e.metaKey;
  if ((e.key === "f" || e.key === "F") && !mod && !e.altKey) {
    e.preventDefault();
    module._anim_recenter();
    return;
  }
  if (e.code === "Space") {
    e.preventDefault();
    module._anim_set_playing(module._anim_get_playing() ? 0 : 1);
    syncChrome();
    saveUi();
  }
});

async function poll() {
  try {
    const s = await fetch("/api/status", { cache: "no-store" }).then((r) => r.json());
    if (s.building) busyEl.textContent = "Compiling…";
    else if (!booted && !s.ok) busyEl.textContent = "Compile failed";
    else busyEl.textContent = "";
    if (!s.ok && s.error) showError(s.error);
    else if (s.ok) hideError();
    if (s.ok && s.rev && s.rev !== seenRev) {
      if (booted) {
        location.reload();
        return;
      }
      seenRev = s.rev;
      try {
        await boot(s.rev);
        booted = true;
        busyEl.textContent = "";
      } catch (err) {
        showError(String(err && err.stack ? err.stack : err));
      }
    }
  } catch (err) {
    if (!booted) busyEl.textContent = "Waiting for the server…";
  }
  setTimeout(poll, 400);
}

poll();
