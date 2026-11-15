#pragma once
//----- Palette -----||
// A Vert::color is a uint16 index into one global table of unlit RGB.
// The viewer looks the index up and shades it from the vertex normal.
//
// palettes.h contract. A modeler export should write this header, and only this:
//
//   #pragma once
//   #include <stdint.h>
//   enum {
//       PAL_NONE = 0,          // reserved, black
//       PAL_STONE = 1,         // one name per color; the value is the index
//       PAL_MOSS = 2,
//       PAL_COUNT
//   };
//   static const uint8_t Anim_palette[][3] = {
//       { 0, 0, 0 },           // 0  PAL_NONE
//       { 196, 198, 202 },     // 1  PAL_STONE   unlit RGB, each channel 0..255
//       { 146, 168, 132 },     // 2  PAL_MOSS
//   };
//   static const int Anim_palette_count = PAL_COUNT;
//
// - One table. Several palettes are concatenated. The name's value is the
//   global index stored in the vertex, not a local index inside one palette.
// - Indices are dense from 0. A gap still needs a row. Count is the row count.
// - Each row is 3 bytes, tightly packed, no padding. Order is R, G, B. Unlit.
// - Anim_palette and Anim_palette_count are those exact names. Count matches
//   the array, and it is a constant expression. Index 0 is reserved and black.
// - The header is include-guarded. It includes `<stdint.h>` and nothing else.
// - Name the file palettes.h. The include path searches models/ before the
//   scene directory, so models/palettes.h wins over one beside the scene.
//   When the header is present the viewer binds it before scene_init.
// - anim_palette_set(rgb, count) replaces that table. rgb points at count * 3
//   bytes that stay alive (static storage). A null rgb or a count below 1
//   selects the built-in fallback.
// - With no palettes.h on the include path, the viewer uses a built-in debug
//   palette (16 solid colors, repeating, index 0 a visible grey) so the build
//   and the draw still work.

#include <stdint.h>

void anim_palette_set(const uint8_t *rgb, int count);

#if __has_include("palettes.h")
#include "palettes.h"
static_assert(sizeof(Anim_palette[0]) == 3, "Anim_palette rows are 3 bytes");
static_assert((int)(sizeof(Anim_palette) / sizeof(Anim_palette[0])) == Anim_palette_count,
              "Anim_palette_count does not match Anim_palette");
inline void anim_palette_bind_default() {
    anim_palette_set(&Anim_palette[0][0], Anim_palette_count);
}
#else
inline void anim_palette_bind_default() {
    anim_palette_set(0, 0);
}
#endif
