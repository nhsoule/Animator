#include "anim.h"
#include "cube.h"

//----- Cube -----||
// Portable update. It only reads its input and writes Anim_out.
// Drop this struct pair and the function into an engine; keep the scene
// tick (sliders, pointer, draws) on the viewer side.

struct Cube_state {
    float angle;
};

struct Cube_in {
    float rate;
    Vec3 pos;
};

inline void Cube_update(Cube_state &s, const Cube_in &in, float dt, Anim_out &out) {
    s.angle += in.rate * dt;
    anim_out_begin(out);
    out.part_n = 1;
    out.part_on[0] = 1;
    out.part[0] = xf_id();
    out.part[0].p = in.pos;
    out.part[0].r = anim_spin(v3(0.f, 0.f, 1.f), s.angle);
}

//----- Scene -----||

static float g_spin = 0.8f;
static Vec3 g_pos = v3(0.f, 0.f, 0.5f);
static Cube_state g_state;
static Anim_mesh g_mesh;

void scene_init() {
    anim_slider("spin", &g_spin, -4.f, 4.f);
    anim_mesh_view(g_mesh, Cube_verts, Cube_vert_count, Cube_offset, Cube_scale);
}

void scene_tick(const Anim_frame &frame) {
    if (frame.pointer.place && frame.pointer.hit && frame.pointer.down) {
        g_pos.x = frame.pointer.world.x;
        g_pos.y = frame.pointer.world.y;
        g_pos.z = 0.5f;
    }

    Anim_out out;
    out.queue = 0;
    Cube_in in;
    in.rate = g_spin;
    in.pos = g_pos;
    Cube_update(g_state, in, frame.dt, out);

    if (out.part_n > 0 && out.part_on[0]) anim_draw(g_mesh, out.part[0]);
}
