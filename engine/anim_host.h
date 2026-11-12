#pragma once
//----- Host -----||
// What a scene file uses. The viewer implements anim_slider and anim_draw.
// An engine port does not include this. It keeps the update headers and writes its own glue.
// The draw list is cleared by the viewer at the start of each frame.

#include "anim_mesh.h"

enum { ANIM_SLIDER_MAX = 16 };
enum { ANIM_DRAW_MAX = 192 };

struct Anim_pointer {
    int place;     // 1 when this gesture is placement, not camera
    int down;
    int pressed;   // went down this frame
    int released;  // went up this frame
    int hit;       // world is a z = 0 ground-plane hit
    Vec3 world;
    float x, y;    // css pixels, origin top-left of the view
};

struct Anim_frame {
    float dt;      // sim step, already scaled, 0 while paused
    float time;    // accumulated sim time
    Anim_pointer pointer;
};

// value must stay alive (a static or a field). The panel edits *value.
void anim_slider(const char *name, float *value, float minv, float maxv);

void anim_draw(const Anim_mesh &mesh, const Transform &xf);

// The scene file defines these two.
void scene_init();
void scene_tick(const Anim_frame &frame);
