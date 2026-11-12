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

canvas.addEventListener("contextmenu", (e) => e.preventDefault());

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
  if (scale !== null) module._anim_set_time_scale(parseFloat(scale));
  const play = sessionStorage.getItem("animator-play");
  if (play !== null) module._anim_set_playing(play === "1" ? 1 : 0);
  const place = sessionStorage.getItem("animator-place");
  if (place !== null) module._anim_set_place(place === "1" ? 1 : 0);
}

function syncChrome() {
  if (!module) return;
  const playing = module._anim_get_playing();
  playBtn.textContent = playing ? "Pause" : "Play";
  const placing = module._anim_get_place();
  placeBtn.classList.toggle("on", !!placing);
  const scale = module._anim_get_time_scale();
  scaleInput.value = String(scale);
  scaleVal.textContent = scale.toFixed(2);
}

function buildSliders() {
  slidersEl.textContent = "";
  const n = module._anim_slider_count();
  for (let i = 0; i < n; i++) {
    const name = module.UTF8ToString(module._anim_slider_name(i));
    const min = module._anim_slider_min(i);
    const max = module._anim_slider_max(i);
    const value = module._anim_slider_value(i);
    const label = document.createElement("label");
    label.className = "slider";
    const val = document.createElement("span");
    val.textContent = value.toFixed(2);
    label.appendChild(document.createTextNode(name + " "));
    label.appendChild(val);
    const input = document.createElement("input");
    input.type = "range";
    input.min = String(min);
    input.max = String(max);
    input.step = "any";
    input.value = String(value);
    input.addEventListener("input", () => {
      const v = parseFloat(input.value);
      module._anim_slider_set(i, v);
      val.textContent = module._anim_slider_value(i).toFixed(2);
      saveUi();
    });
    slidersEl.appendChild(label);
    slidersEl.appendChild(input);
  }
}

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
