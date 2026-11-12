# Animator

Animator is a local sandbox for prototyping animation and behaviour in C++. A scene is plain C++ against a small API: a state struct, an input struct, and an update function that writes part transforms and optional events. The same update compiles unchanged into your own engine. Nothing about a scene is preset. Spawn and remove instances, move parts, rebuild meshes, add sliders, read a ground-plane drag.

The viewer is WebGL2, the same subset as OpenGL ES 3. You compile a scene on your machine with Emscripten. This repo is the tool source and a blank starter. Your own scenes and model headers stay on your machine and are not part of this repo.

Z is up. The API does not allocate per frame, pools are fixed capacity, and updates are plain functions (no virtual dispatch).

## Setup

Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) and Python 3.

```
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

On Windows, run `emsdk_env.bat` from the emsdk directory instead of `source`. `python` can stand in for `python3`. If `emcc` is not on `PATH`, set `EMCC` to the full path of the `emcc` binary (or `emcc.bat`).

## Workflow

From this repo:

```
./anim watch scenes/examples/starter.cpp
```

Windows:

```
python anim.py watch scenes/examples/starter.cpp
```

`--port N` changes the port (the default is 8080, or the next free port).

The command compiles the scene, serves the viewer on `0.0.0.0`, and prints a localhost URL and a LAN URL. A phone on the same Wi-Fi opens the LAN URL. Saving the scene, or any header it includes with quotes, rebuilds it and the page reloads. Compile errors are printed in the terminal and shown on the page. The previous build stays up while a rebuild is failing, with the error over the view.

The view:

- Drag to orbit. Right-drag, middle-drag, or Shift-drag to pan. The wheel zooms.
- Touch: one finger orbits, two fingers pan and pinch. Turn on **Place** and one finger drags on the ground instead; two fingers orbit, pinch zooms, three fingers pan.
- **Place** reports that ground drag to the scene. The hit is the z = 0 plane.
- Pause and the time-scale slider are viewer controls. Space pauses when the focus is not in a field.
- Sliders under those are whatever the scene registered.

A reload starts the scene over. For the rest of the browser tab, slider values, time scale, pause, and Place are restored.

## Model headers

Models are included as headers and compiled in. Each vertex is 12 bytes, tightly packed, three vertices per triangle, CCW from the outside:

| Offset | Field | Type |
|---|---|---|
| 0 | `pos` | `int16[3]`, normalized, −32767..32767 maps to −1..1 |
| 6 | `nrm` | `int8[2]`, octahedral unit normal, outward |
| 8 | `rgba` | `uint8[4]`, alpha 255 |

`engine/vert.h` is the `Vert` struct those headers include. A header named `Name` provides:

```
static const float Name_offset[3];
static const float Name_scale;
static const Vert Name_verts[];
static const int Name_vert_count;
```

Recover a local point with `offset + (pos / 32767) * scale`. The part transform is applied after that, so a translated part is `pivot + offset + (pos / 32767) * scale`. `Name_scale` is half the longest side of the bounds and `Name_offset` is the centre.

Load one into a draw:

```cpp
Anim_mesh mesh;
anim_mesh_view(mesh, Name_verts, Name_vert_count, Name_offset, Name_scale);
anim_draw(mesh, transform);
```

Each draw is one mesh and one transform, so parts of a model are separate headers (or separate meshes) drawn with their own transforms. Hide a part by not drawing it.

`models/examples/cube.h` is a 1-unit placeholder cube in that layout. Put your own headers in `models/` or next to the scene. Both `scenes/` and `models/` are gitignored except `scenes/examples/` and `models/examples/`.

You can also build meshes in code (`anim_box`, `anim_cylinder_z`, `anim_tube`) into an `Anim_scratch`, then `anim_pack` them into a `Vert` buffer you own. `Anim_scratch` is large; keep it in static storage, not on the stack. Bump `mesh.rev` by calling `anim_pack` again when the bytes change so the viewer reuploads them.

## An update

An update is the piece that drops into an engine. It does not read a sim, the clock, or the pointer. Glue fills the input.

```cpp
struct Thing_state {
    float angle;
};

struct Thing_in {
    float rate;
    Vec3 pos;
};

inline void Thing_update(Thing_state &s, const Thing_in &in, float dt, Anim_out &out) {
    s.angle += in.rate * dt;
    anim_out_begin(out);
    out.part_n = 1;
    out.part_on[0] = 1;
    out.part[0] = xf_id();
    out.part[0].p = in.pos;
    out.part[0].r = anim_spin(v3(0.f, 0.f, 1.f), s.angle);
}
```

`out.part[]` is a world transform per part (`ANIM_PART_MAX`). `part_on[i] = 0` hides that part. The viewer does not read `Anim_out` itself; the scene tick decides what to draw.

Events go on a queue the caller owns. Apply them after the update loop, not during it. Extra events past `ANIM_EVENT_MAX` are dropped.

```cpp
Anim_queue queue;
anim_queue_clear(queue);

Anim_out out;
out.queue = &queue;
Thing_update(state, in, dt, out);

anim_out_spawn(out, TYPE_THING, payload, 4);   // or anim_spawn(queue, ...)
anim_out_remove(out, TYPE_THING, handle);
anim_out_emit(out, CUSTOM_ID, payload, 4);      // custom id + up to 4 floats

for (int i = 0; i < queue.n; i++) {
    Anim_event &e = queue.ev[i];
    if (e.kind == ANIM_EV_SPAWN) { /* pool_spawn, read e.payload */ }
    if (e.kind == ANIM_EV_REMOVE) { /* pool_remove the handle in e.index / e.gen */ }
    if (e.kind == ANIM_EV_CUSTOM) { /* e.custom_id, e.payload */ }
}
anim_queue_clear(queue);
```

Instances live in a fixed pool. Removal is swap-and-pop on the live list. A handle is an index plus a generation; `anim_pool_get` returns null when the generation does not match.

```cpp
struct Thing_state { float angle; };
Anim_pool<Thing_state, 32> pool;
anim_pool_init(pool);
Anim_handle h = anim_pool_spawn(pool);
Thing_state *s = anim_pool_get(pool, h);
anim_pool_remove(pool, h);
```

Keep the pooled struct plain. Spawn zeroes it with `memset`.

## Helpers

`anim_math.h`:

- `Vec3`, `Quat`, `Transform`. `xf_id`, `xf_mul` (parent then local; parent scale should be uniform), `xf_point`, `xf_span` (unit cylinder from z = 0..1 placed from A to B), `xf_to_mat` (column-major T * R * S).
- `anim_lerp`, `anim_lerp3`, `anim_repeat` (positive wrap).
- `anim_bob(t, hz, amp)` vertical offset.
- `anim_sway(axis, t, hz, amp)` a rocking quaternion.
- `anim_spin(axis, radians)`.
- `anim_catenary(a, b, sag, t)` with `t` in 0..1. `sag` is the drop of the midpoint under the straight chord. The shape is a normalized cosh, not a tension solve.
- `anim_path_build` / `anim_path_at` sample a polyline. Distance wraps, so `anim_path_at(path, pts, travel, &tangent)` loops.

`anim_mesh.h` builds placeholder geometry and packs it with the same offset/scale rule as a model header. `anim.h` includes the lot for a scene file. An update header that must compile in an engine should include `anim_math.h` and `anim_out.h` only (and `anim_pool.h` if it uses pools).

Caps live next to the types they bound: `ANIM_PART_MAX`, `ANIM_EVENT_MAX`, `ANIM_PAYLOAD`, `ANIM_PATH_MAX`, `ANIM_SCRATCH_MAX`, `ANIM_SLIDER_MAX`, `ANIM_DRAW_MAX`. The viewer also keeps a fixed GPU-buffer cache (96 meshes).

## A scene

A scene file defines two functions:

```cpp
void scene_init();
void scene_tick(const Anim_frame &frame);
```

`scene_init` registers sliders and views meshes. The float you pass to `anim_slider` must stay alive (a static or a field). The panel edits it.

```cpp
static float g_spin = 0.8f;

void scene_init() {
    anim_slider("spin", &g_spin, -4.f, 4.f);
}
```

`scene_tick` runs every frame. `frame.dt` is the sim step: 0 while paused, already multiplied by the time scale, and clamped. `frame.time` is the accumulated sim time. `frame.pointer` is the ground pick:

- `place` is 1 while Place mode is on
- `down`, `pressed`, `released` describe the current placement gesture
- `hit` is 1 when `world` lies on z = 0
- `x`, `y` are css pixels in the view

The tick fills inputs, calls updates, applies events, and submits draws. Draws are for the current frame only; the list is cleared before the tick.

`scenes/examples/starter.cpp` is the blank template: it includes `cube.h`, registers one slider, and on a Place drag moves the cube to the ground hit. Copy it to `scenes/your_scene.cpp` (gitignored) and point the include at your header. `./anim watch` adds these include directories: `engine/`, `models/`, `models/examples/`, the scene's own directory, and the repo root.

The viewer multiplies vertex color by a simple ambient plus one sun, so unlit meshes read as solid objects. Colors that were already baked with a light get a bit brighter.
