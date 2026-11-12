#pragma once
//----- Mesh -----||
// CPU mesh builder that packs into Vert. Scratch is caller-owned and fixed-capacity
// so a frame never allocates. Pack uses the same offset/scale rule as an exported header:
//   scale  = half the longest side of the bounds
//   offset = centre
//   world  = offset + (pos / 32767) * scale

#include "vert.h"
#include "anim_math.h"

enum { ANIM_SCRATCH_MAX = 8192 };

struct Anim_scratch {
    float pos[ANIM_SCRATCH_MAX * 3];
    float nrm[ANIM_SCRATCH_MAX * 3];
    uint8_t col[ANIM_SCRATCH_MAX * 4];
    int count;
};

struct Anim_mesh {
    const Vert *verts;
    int count;
    Vec3 offset;
    float scale;
    int rev;
};

inline void anim_scratch_clear(Anim_scratch &s) { s.count = 0; }

inline void anim_mesh_view(Anim_mesh &m, const Vert *verts, int count, const float offset[3], float scale) {
    m.verts = verts;
    m.count = count;
    m.offset = offset ? v3(offset[0], offset[1], offset[2]) : v3(0.f, 0.f, 0.f);
    m.scale = scale > 0.f ? scale : 1.f;
    m.rev = 0;
}

inline void anim_tri(Anim_scratch &s, Vec3 a, Vec3 b, Vec3 c, uint8_t r, uint8_t g, uint8_t b8) {
    if (s.count + 3 > ANIM_SCRATCH_MAX) return;
    Vec3 n = v3_cross(v3_sub(b, a), v3_sub(c, a));
    if (v3_len2(n) < 1e-12f) return;
    n = v3_norm(n);
    Vec3 p[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        int k = s.count++;
        s.pos[k * 3 + 0] = p[i].x;
        s.pos[k * 3 + 1] = p[i].y;
        s.pos[k * 3 + 2] = p[i].z;
        s.nrm[k * 3 + 0] = n.x;
        s.nrm[k * 3 + 1] = n.y;
        s.nrm[k * 3 + 2] = n.z;
        s.col[k * 4 + 0] = r;
        s.col[k * 4 + 1] = g;
        s.col[k * 4 + 2] = b8;
        s.col[k * 4 + 3] = 255;
    }
}

inline void anim_box(Anim_scratch &s, Vec3 center, Vec3 size, uint8_t r, uint8_t g, uint8_t b) {
    Vec3 h = v3_scale(size, 0.5f);
    Vec3 p[8];
    for (int i = 0; i < 8; i++) {
        float sx = (i & 1) ? h.x : -h.x;
        float sy = (i & 2) ? h.y : -h.y;
        float sz = (i & 4) ? h.z : -h.z;
        p[i] = v3_add(center, v3(sx, sy, sz));
    }
    // bit 0 = +x, bit 1 = +y, bit 2 = +z. CCW from the outside.
    anim_tri(s, p[4], p[5], p[7], r, g, b);
    anim_tri(s, p[4], p[7], p[6], r, g, b);
    anim_tri(s, p[0], p[3], p[1], r, g, b);
    anim_tri(s, p[0], p[2], p[3], r, g, b);
    anim_tri(s, p[2], p[7], p[3], r, g, b);
    anim_tri(s, p[2], p[6], p[7], r, g, b);
    anim_tri(s, p[0], p[1], p[5], r, g, b);
    anim_tri(s, p[0], p[5], p[4], r, g, b);
    anim_tri(s, p[1], p[3], p[7], r, g, b);
    anim_tri(s, p[1], p[7], p[5], r, g, b);
    anim_tri(s, p[0], p[4], p[6], r, g, b);
    anim_tri(s, p[0], p[6], p[2], r, g, b);
}

// Cylinder along +Z. radius in XY, from z0 to z1. sides clamped to 3..12.
inline void anim_cylinder_z(Anim_scratch &s, float z0, float z1, float radius, int sides,
                            uint8_t r, uint8_t g, uint8_t b, int caps) {
    if (sides < 3) sides = 3;
    if (sides > 12) sides = 12;
    Vec3 bot[12], top[12];
    for (int i = 0; i < sides; i++) {
        float a = 6.28318530718f * (float)i / (float)sides;
        float x = cosf(a) * radius;
        float y = sinf(a) * radius;
        bot[i] = v3(x, y, z0);
        top[i] = v3(x, y, z1);
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        anim_tri(s, bot[i], top[j], top[i], r, g, b);
        anim_tri(s, bot[i], bot[j], top[j], r, g, b);
    }
    if (!caps) return;
    Vec3 cb = v3(0.f, 0.f, z0);
    Vec3 ct = v3(0.f, 0.f, z1);
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        anim_tri(s, top[i], top[j], ct, r, g, b);
        anim_tri(s, bot[i], cb, bot[j], r, g, b);
    }
}

// Tube along a polyline. No caps. sides clamped to 3..8. CCW outward.
inline void anim_tube(Anim_scratch &s, const Vec3 *p, int n, float radius, int sides,
                      uint8_t r, uint8_t g, uint8_t b) {
    if (!p || n < 2) return;
    if (sides < 3) sides = 3;
    if (sides > 8) sides = 8;
    Vec3 prev[8];
    int have = 0;
    for (int i = 0; i < n; i++) {
        Vec3 dir;
        if (i == 0) dir = v3_sub(p[1], p[0]);
        else if (i == n - 1) dir = v3_sub(p[i], p[i - 1]);
        else dir = v3_sub(p[i + 1], p[i - 1]);
        if (v3_len2(dir) < 1e-10f) dir = v3(0.f, 0.f, 1.f);
        else dir = v3_norm(dir);
        Vec3 up = (fabsf(dir.z) > 0.9f) ? v3(0.f, 1.f, 0.f) : v3(0.f, 0.f, 1.f);
        Vec3 side = v3_norm(v3_cross(dir, up));
        Vec3 bin = v3_cross(side, dir);
        Vec3 ring[8];
        for (int k = 0; k < sides; k++) {
            float a = 6.28318530718f * (float)k / (float)sides;
            ring[k] = v3_add(p[i], v3_add(v3_scale(side, cosf(a) * radius), v3_scale(bin, sinf(a) * radius)));
        }
        if (have) {
            for (int k = 0; k < sides; k++) {
                int k1 = (k + 1) % sides;
                anim_tri(s, prev[k], ring[k], ring[k1], r, g, b);
                anim_tri(s, prev[k], ring[k1], prev[k1], r, g, b);
            }
        }
        for (int k = 0; k < sides; k++) prev[k] = ring[k];
        have = 1;
    }
}

inline int anim_q16(float v) {
    int q = (int)lroundf(v);
    if (q > 32767) q = 32767;
    if (q < -32767) q = -32767;
    return q;
}

// Pack scratch into dst (cap verts). m points at dst. rev increments so a viewer can reupload.
inline void anim_pack(const Anim_scratch &s, Vert *dst, int cap, Anim_mesh &m) {
    m.verts = dst;
    m.count = 0;
    m.offset = v3(0.f, 0.f, 0.f);
    m.scale = 1.f;
    m.rev++;
    if (!dst || cap < 1 || s.count < 1) return;
    int n = s.count;
    if (n > cap) n = cap;
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) {
            float v = s.pos[i * 3 + k];
            if (v < lo[k]) lo[k] = v;
            if (v > hi[k]) hi[k] = v;
        }
    }
    float offset[3];
    float scale = 0.f;
    for (int k = 0; k < 3; k++) {
        offset[k] = (lo[k] + hi[k]) * 0.5f;
        float half = (hi[k] - lo[k]) * 0.5f;
        if (half > scale) scale = half;
    }
    if (!(scale > 0.f)) scale = 1.f;
    for (int i = 0; i < n; i++) {
        Vert &v = dst[i];
        for (int k = 0; k < 3; k++) {
            v.pos[k] = (int16_t)anim_q16((s.pos[i * 3 + k] - offset[k]) / scale * 32767.f);
        }
        vert_oct_encode(s.nrm[i * 3], s.nrm[i * 3 + 1], s.nrm[i * 3 + 2], v.nrm);
        v.rgba[0] = s.col[i * 4];
        v.rgba[1] = s.col[i * 4 + 1];
        v.rgba[2] = s.col[i * 4 + 2];
        v.rgba[3] = s.col[i * 4 + 3];
    }
    m.verts = dst;
    m.count = n;
    m.offset = v3(offset[0], offset[1], offset[2]);
    m.scale = scale;
}
