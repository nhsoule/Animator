#pragma once
//----- Math -----||
// Small vec/quat/transform helpers. No allocation. Z is up.
// Transform scale is applied in local space (matrix is T * R * S).
// xf_mul assumes the parent scale is uniform.

#include <math.h>
#include <string.h>

struct Vec3 {
    float x, y, z;
};

struct Quat {
    float x, y, z, w;
};

struct Transform {
    Vec3 p;
    Quat r;
    Vec3 s;
};

inline Vec3 v3(float x, float y, float z) {
    Vec3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 v3_scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
inline Vec3 v3_mul(Vec3 a, Vec3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 v3_cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline float v3_len2(Vec3 a) { return v3_dot(a, a); }

inline float v3_len(Vec3 a) { return sqrtf(v3_len2(a)); }

inline Vec3 v3_norm(Vec3 a) {
    float L = v3_len(a);
    if (L < 1e-8f) return v3(0.f, 0.f, 1.f);
    return v3_scale(a, 1.f / L);
}

inline Quat quat_id() {
    Quat q;
    q.x = 0.f;
    q.y = 0.f;
    q.z = 0.f;
    q.w = 1.f;
    return q;
}

inline Quat quat_norm(Quat q) {
    float L = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (L < 1e-8f) return quat_id();
    float s = 1.f / L;
    q.x *= s;
    q.y *= s;
    q.z *= s;
    q.w *= s;
    return q;
}

inline Quat quat_mul(Quat a, Quat b) {
    Quat q;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return q;
}

inline Vec3 quat_rot(Quat q, Vec3 v) {
    Vec3 u = v3(q.x, q.y, q.z);
    Vec3 t = v3_scale(v3_cross(u, v), 2.f);
    return v3_add(v, v3_add(v3_scale(t, q.w), v3_cross(u, t)));
}

inline Quat quat_axis_angle(Vec3 axis, float radians) {
    float h = radians * 0.5f;
    float s = sinf(h);
    Vec3 a = v3_norm(axis);
    Quat q;
    q.x = a.x * s;
    q.y = a.y * s;
    q.z = a.z * s;
    q.w = cosf(h);
    return q;
}

inline Quat quat_from_to(Vec3 a, Vec3 b) {
    a = v3_norm(a);
    b = v3_norm(b);
    float d = v3_dot(a, b);
    if (d > 0.9999f) return quat_id();
    if (d < -0.9999f) {
        Vec3 ax = v3_cross(v3(1.f, 0.f, 0.f), a);
        if (v3_len2(ax) < 1e-6f) ax = v3_cross(v3(0.f, 1.f, 0.f), a);
        return quat_axis_angle(ax, 3.14159265f);
    }
    Vec3 c = v3_cross(a, b);
    Quat q;
    q.x = c.x;
    q.y = c.y;
    q.z = c.z;
    q.w = 1.f + d;
    return quat_norm(q);
}

// Columns are the images of local +X, +Y, +Z. Right-handed, columns unit length.
inline Quat quat_from_basis(Vec3 X, Vec3 Y, Vec3 Z) {
    float m00 = X.x, m10 = X.y, m20 = X.z;
    float m01 = Y.x, m11 = Y.y, m21 = Y.z;
    float m02 = Z.x, m12 = Z.y, m22 = Z.z;
    Quat q;
    float tr = m00 + m11 + m22;
    if (tr > 0.f) {
        float s = sqrtf(tr + 1.f) * 2.f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.f + m00 - m11 - m22) * 2.f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.f + m11 - m00 - m22) * 2.f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.f + m22 - m00 - m11) * 2.f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return quat_norm(q);
}

inline Transform xf_id() {
    Transform t;
    t.p = v3(0.f, 0.f, 0.f);
    t.r = quat_id();
    t.s = v3(1.f, 1.f, 1.f);
    return t;
}

inline Vec3 xf_point(const Transform &t, Vec3 local) {
    return v3_add(t.p, quat_rot(t.r, v3_mul(t.s, local)));
}

inline Transform xf_mul(const Transform &parent, const Transform &local) {
    Transform o;
    o.r = quat_mul(parent.r, local.r);
    o.s = v3_mul(parent.s, local.s);
    o.p = v3_add(parent.p, quat_rot(parent.r, v3_mul(parent.s, local.p)));
    return o;
}

// Unit cylinder mesh (radius 1, along +Z from 0 to 1) stretched from a to b.
inline Transform xf_span(Vec3 a, Vec3 b, float radius) {
    Vec3 d = v3_sub(b, a);
    float len = v3_len(d);
    Transform t = xf_id();
    t.p = a;
    if (len < 1e-5f) {
        t.s = v3(radius, radius, 1e-4f);
        return t;
    }
    t.r = quat_from_to(v3(0.f, 0.f, 1.f), v3_scale(d, 1.f / len));
    t.s = v3(radius, radius, len);
    return t;
}

// Column-major 4x4, T * R * S.
inline void xf_to_mat(const Transform &t, float m[16]) {
    Quat q = quat_norm(t.r);
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    m[0] = (1.f - 2.f * (yy + zz)) * t.s.x;
    m[1] = (2.f * (xy + wz)) * t.s.x;
    m[2] = (2.f * (xz - wy)) * t.s.x;
    m[3] = 0.f;
    m[4] = (2.f * (xy - wz)) * t.s.y;
    m[5] = (1.f - 2.f * (xx + zz)) * t.s.y;
    m[6] = (2.f * (yz + wx)) * t.s.y;
    m[7] = 0.f;
    m[8] = (2.f * (xz + wy)) * t.s.z;
    m[9] = (2.f * (yz - wx)) * t.s.z;
    m[10] = (1.f - 2.f * (xx + yy)) * t.s.z;
    m[11] = 0.f;
    m[12] = t.p.x;
    m[13] = t.p.y;
    m[14] = t.p.z;
    m[15] = 1.f;
}

//----- Lerp, bob, sway, spin -----||

inline float anim_lerp(float a, float b, float t) { return a + (b - a) * t; }

inline Vec3 anim_lerp3(Vec3 a, Vec3 b, float t) {
    return v3(anim_lerp(a.x, b.x, t), anim_lerp(a.y, b.y, t), anim_lerp(a.z, b.z, t));
}

// Positive modulo. Useful for wrapping a distance onto a loop.
inline float anim_repeat(float d, float len) {
    if (len <= 1e-6f) return 0.f;
    d = fmodf(d, len);
    if (d < 0.f) d += len;
    return d;
}

inline float anim_bob(float t, float hz, float amp) {
    return sinf(t * hz * 6.28318530718f) * amp;
}

inline Quat anim_sway(Vec3 axis, float t, float hz, float amp) {
    return quat_axis_angle(axis, sinf(t * hz * 6.28318530718f) * amp);
}

inline Quat anim_spin(Vec3 axis, float radians) {
    return quat_axis_angle(axis, radians);
}

//----- Catenary -----||
// Vertical drop under the straight chord. sag is the drop at mid-span (world units).
// The profile is a normalized cosh, so the ends stay put and the middle falls by sag.
// Cheap stand-in for a hanging rope: no iteration, no allocation.
inline Vec3 anim_catenary(Vec3 a, Vec3 b, float sag, float t) {
    Vec3 p = anim_lerp3(a, b, t);
    const float k = 2.5f;
    float mid = coshf(k * 0.5f);
    float denom = 1.f - mid;
    float w = 0.f;
    if (fabsf(denom) > 1e-6f) w = (coshf(k * (t - 0.5f)) - mid) / denom;
    p.z -= sag * w;
    return p;
}

//----- Path -----||
// Looping position along a polyline. cum[] is filled by anim_path_build.
// dist wraps with anim_repeat, so a caller can loop with fmod(travel, length).

enum { ANIM_PATH_MAX = 512 };

struct Anim_path {
    float cum[ANIM_PATH_MAX];
    int n;
    float len;
};

inline void anim_path_build(Anim_path &path, const Vec3 *p, int n) {
    if (n < 1) n = 0;
    if (n > ANIM_PATH_MAX) n = ANIM_PATH_MAX;
    path.n = n;
    path.len = 0.f;
    if (n < 1) return;
    path.cum[0] = 0.f;
    for (int i = 1; i < n; i++) {
        path.cum[i] = path.cum[i - 1] + v3_len(v3_sub(p[i], p[i - 1]));
    }
    path.len = path.cum[n - 1];
}

// Writes a unit tangent when tangent != 0. dist may be any real; it loops.
inline Vec3 anim_path_at(const Anim_path &path, const Vec3 *p, float dist, Vec3 *tangent) {
    if (path.n < 1) {
        if (tangent) *tangent = v3(0.f, 1.f, 0.f);
        return v3(0.f, 0.f, 0.f);
    }
    if (path.n == 1 || path.len < 1e-6f) {
        if (tangent) *tangent = v3(0.f, 1.f, 0.f);
        return p[0];
    }
    dist = anim_repeat(dist, path.len);
    int i = 1;
    while (i < path.n - 1 && path.cum[i] < dist) i++;
    float seg = path.cum[i] - path.cum[i - 1];
    float u = (seg > 1e-6f) ? (dist - path.cum[i - 1]) / seg : 0.f;
    Vec3 d = v3_sub(p[i], p[i - 1]);
    if (tangent) *tangent = v3_norm(d);
    return v3_add(p[i - 1], v3_scale(d, u));
}
