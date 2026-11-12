#pragma once
//----- Vert -----||
// Packed vertex shared with exported model headers. 12 bytes, stride 12, no padding.
// 3 verts per triangle, CCW seen from outside. Z is up.
//   pos  int16x3 @0, normalized: -32767..32767 maps to -1..1
//   nrm  int8x2 octahedral @6, normalized, unit normal (outward)
//   rgba uint8x4 @8 (alpha is 255)
// A header also ships name_offset[3] and name_scale. With a part pivot transform P:
//   local = offset + (pos / 32767) * scale
//   world = P * local
// which is pivot + offset + (pos / 32767) * scale when P is a translation.

#include <stdint.h>
#include <stddef.h>
#include <math.h>

struct Vert {
    int16_t pos[3];
    int8_t nrm[2];
    uint8_t rgba[4];
};

static_assert(sizeof(Vert) == 12, "Vert is 12 bytes");
static_assert(offsetof(Vert, pos) == 0, "pos at 0");
static_assert(offsetof(Vert, nrm) == 6, "nrm at 6");
static_assert(offsetof(Vert, rgba) == 8, "rgba at 8");

//----- Octahedral normal -----||
// Matches the usual octahedral encode: project onto the L1 octahedron, fold the
// lower hemisphere, quantize to -127..127.
// Decode (the viewer shader does this). e is the normalized pair, in -1..1:
//   n = vec3(e, 1 - abs(e.x) - abs(e.y));
//   if (n.z < 0) n.xy = (1 - abs(n.yx)) * vec2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
//   n = normalize(n);
inline void vert_oct_encode(float x, float y, float z, int8_t out[2]) {
    float l1 = fabsf(x) + fabsf(y) + fabsf(z);
    if (l1 < 1e-8f) l1 = 1.f;
    x /= l1;
    y /= l1;
    z /= l1;
    if (z < 0.f) {
        float ox = x, oy = y;
        x = (1.f - fabsf(oy)) * (ox >= 0.f ? 1.f : -1.f);
        y = (1.f - fabsf(ox)) * (oy >= 0.f ? 1.f : -1.f);
    }
    int qx = (int)lroundf(x * 127.f);
    int qy = (int)lroundf(y * 127.f);
    if (qx > 127) qx = 127;
    if (qx < -127) qx = -127;
    if (qy > 127) qy = 127;
    if (qy < -127) qy = -127;
    out[0] = (int8_t)qx;
    out[1] = (int8_t)qy;
}
