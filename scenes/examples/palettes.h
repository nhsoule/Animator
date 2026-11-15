#pragma once
//----- Palettes -----||
// Example of the palettes.h contract in engine/anim_palette.h.
#include <stdint.h>
// One global table. A Vert::color index is the enumerator's value.
// Rows are unlit RGB, 0..255. The viewer shades them from the normal.

enum {
    PAL_NONE = 0,

    // stone
    PAL_STONE = 1,

    // ground
    PAL_MOSS = 2,
    PAL_SAND = 3,
    PAL_CLAY = 4,

    PAL_COUNT
};

static const uint8_t Anim_palette[][3] = {
    { 0, 0, 0 },
    { 196, 198, 202 },
    { 146, 168, 132 },
    { 206, 186, 140 },
    { 176, 140, 118 },
};
static const int Anim_palette_count = PAL_COUNT;
