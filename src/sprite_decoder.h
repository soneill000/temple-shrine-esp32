// sprite_decoder.h — infrastructure for decoding Terry's DolDoc sprites.
//
// Verified opcode values are taken from Adam/Gr/Gr.HH in the TempleOS
// source. A sprite is a small stream of {u16 opcode, opcode-specific
// payload} records, terminated by SPT_END.
//
// This header defines the enum and a small API. The runtime decoder
// (DolDoc envelope parser + per-opcode rasterizer) is a follow-up
// project — see src/harness/SPRITE_NOTES.md for the plan.

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SPT_END               = 0,
    SPT_COLOR             = 1,
    SPT_DITHER_COLOR      = 2,
    SPT_THICK             = 3,
    SPT_PLANAR_SYMMETRY   = 4,
    SPT_TRANSFORM_ON      = 5,
    SPT_TRANSFORM_OFF     = 6,
    SPT_SHIFT             = 7,
    SPT_PT                = 8,
    SPT_POLYPT            = 9,
    SPT_LINE              = 10,
    SPT_POLYLINE          = 11,
    SPT_RECT              = 12,
    SPT_ROTATED_RECT      = 13,
    SPT_CIRCLE            = 14,
    SPT_ELLIPSE           = 15,
    SPT_POLYGON           = 16,
    SPT_BSPLINE2          = 17,
    SPT_BSPLINE2_CLOSED   = 18,
    SPT_BSPLINE3          = 19,
    SPT_BSPLINE3_CLOSED   = 20,
    SPT_FLOOD_FILL        = 21,
    SPT_FLOOD_FILL_NOT    = 22,
    SPT_BITMAP            = 23,
    SPT_MESH              = 24,
    SPT_SHIFTABLE_MESH    = 25,
    SPT_ARROW             = 26,
    SPT_TEXT              = 27,
    SPT_TEXT_BOX          = 28,
    SPT_TEXT_DIAMOND      = 29,
    SPT_TYPES_NUM         = 30,
} spt_opcode_t;

// DolDoc element types we care about while scanning the tail. Values from
// Kernel/KernelA.HH. There are many more; these are the ones that carry
// sprite data.
#define DOCT_SPRITE         37
#define DOCT_INS_BIN        38
#define DOCT_INS_BIN_SIZE   39

// A decoded reference to one sprite payload embedded in a .HC file:
// `num` is the BI= index used to reference the sprite from source text,
// `data` points into the caller-provided tail buffer (borrowed, not owned).
typedef struct {
    int             num;
    uint32_t        size;
    const uint8_t  *data;
} sprite_ref_t;

// Scan a DolDoc "tail" (the byte range after the first NUL in a .HC file)
// and populate `out[]` with references to each sprite payload found.
// Returns the number of sprites written, or -1 on parse error.
//
// TODO(session-next): implement. This is the first thing to write once
// we sit down for the sprite decoder in earnest. See SPRITE_NOTES.md
// for the CDocBin header layout to expect.
int sprite_scan_tail(const uint8_t *tail, uint32_t tail_size,
                     sprite_ref_t *out, int max_out);

// Rasterize a sprite through the shrine shim, positioned at (x, y).
void sprite_render(const sprite_ref_t *sp, int x, int y);

// Rasterize a raw op-stream buffer through the shrine shim.
// Coordinates in the stream are signed offsets from (cx, cy).
// Covered ops: END, COLOR, DITHER_COLOR (approx), THICK, PT, SHIFT,
// LINE, ARROW, RECT, CIRCLE, TRANSFORM_ON/OFF. FLOOD_FILL is a no-op.
void sprite_render_stream(const uint8_t *stream, unsigned size,
                          int cx, int cy);
