# TempleOS sprite / DolDoc binary format — research notes

The goal: parse the binary sprite payloads embedded in `.HC` files (like
`BomberGolf.HC`'s tank/bunker/plane sprites) and render them through our
shim, so games can use Terry's actual art instead of my procedural
stand-ins.

Below is what we've mapped so far. This is a research log, not a spec —
verify against real files as you go.

## File layout of a `.HC` with sprites

A `.HC` file is a **DolDoc document**:

1. **Text region** — the source code plus embedded formatting tokens
   like `$FG,YELLOW$` for colored text and `$SP,"<N>",BI=N$` for a
   sprite reference (BI = "bin index").
2. **NUL terminator** (single `\0`).
3. **Tail: appended `CDocBin` records.** Each record has metadata
   (`num`, `size`, `use_cnt`) followed by a raw payload — for sprites,
   a packed `CSprite` stream.

You can eyeball the transition in a hex dump — everything after the first
0x00 in a `.HC` file is the binary tail.

## `CDocBin` header (from `KernelA.HH` / `Kernel/*` in the repo)

The relevant defines live in `Kernel/KernelA.HH` under `DOCT_*` (document
element types). Key ones:

| Constant | Value | Meaning |
|---|---|---|
| `DOCT_SPRITE` | 37 | Sprite reference in document text |
| `DOCT_INS_BIN` | 38 | Marks an inline binary attachment |
| `DOCT_INS_BIN_SIZE` | 39 | Followed by the size of the payload |

Format sketch (verify against `Kernel/DocSup*.HC` before coding):

```
u16 flags          (DOCEF_* bits — DFT_LEN, DFT_RAW_TYPE, DONT_DRAW, etc)
u64 raw_type       (application-defined, e.g. an "AR_" constant)
u32 num            (BI= index; used to look up references)
u32 size           (payload byte count)
u8  payload[size]  (the sprite command stream itself)
```

## `CSprite` payload

**Not a bitmap.** It's a vector command stream — a sequence of drawing
opcodes with color/coordinate/vertex data inline. Reference code lives in:

- `Adam/Gr/GrSpritePlot.HC` — the rasterizer (walks the ops, calls
  `GrLine`, `GrFillPoly`, `GrCircle`, etc.)
- `Adam/Gr/SpriteBitMap.HC` — the bitmap-sprite subset
- `Adam/Gr/GrExt.HC` — extended ops
- `Adam/Gr/Gr.HH` — type definitions for `CSprite`, `CSpriteBase`,
  `CSpritePolyLine`, etc.

Each op begins with a `u16` type followed by op-specific fields. The
common ones you'll encounter in game sprites are:

| Op | Typical fields |
|---|---|
| `SPT_END`  | (marks end of stream) |
| `SPT_COLOR` | `u32 color` |
| `SPT_POINT` | `i32 x, y` |
| `SPT_LINE` | `i32 x1, y1, x2, y2` |
| `SPT_RECT` | `i32 x, y, w, h` |
| `SPT_POLYGON` | `u32 n; { i32 x, y } × n` |
| `SPT_FLOOD_FILL` | `i32 x, y` |
| `SPT_TEXT` | `i32 x, y; u8 chars[]` |
| `SPT_BITMAP` | `u32 w, h; u8 pixels[w * h]` (indexed color) |

**Exact opcode numbers must be pulled from `Adam/Gr/Gr.HH`** —
transcribe them, don't guess.

## Implementation plan (roughly one weekend)

**Step 1: DolDoc tail parser.** Read a `.HC` file, split at the first
NUL, iterate `CDocBin` records in the tail, collect them into a
`{ num -> {size, payload} }` map. Test against `BomberGolf.HC` — Terry
tags his sprites `BI=1` through `BI=10`. All ten should decode.

**Step 2: `CSprite` walker.** Iterate op codes in a payload. Handle
`SPT_END`, `SPT_COLOR`, `SPT_LINE`, `SPT_POLYGON`, `SPT_RECT`,
`SPT_FLOOD_FILL` first — those cover ~90% of Terry's 2D game sprites.

**Step 3: Render through the shim.** Map each op to `shrine_*`:
- `SPT_COLOR` → track current color
- `SPT_LINE` → `shrine_line`
- `SPT_RECT` → `shrine_fill_rect`
- `SPT_POLYGON` → scanline fill (write a simple polygon filler; ~50 lines)
- `SPT_FLOOD_FILL` → this one's harder; may skip for MVP or implement
  a small scanline flood-fill since we're on 320×240 in memory

**Step 4: Test in the SDL harness.** Load `BomberGolf.HC`, decode the
tank sprite (BI=3), render it at a fixed spot. Iterate until it looks
right. Compare against a screenshot of TempleOS running BomberGolf.

**Step 5: Ship it.** Extract the sprites we want at build time from
`.HC` files, embed them as C arrays via a codegen step, and re-skin our
Terry ports (BomberGolf, real FlapBat, real Whap) with authentic art.

## Files worth downloading now

```
Kernel/KernelA.HH            — DOCT_* / DOCEF_* / AR_* defines
Kernel/DocSup*.HC            — DolDoc serialization functions
Adam/Gr/Gr.HH                — CSprite / SPT_* type + opcode defs
Adam/Gr/GrSpritePlot.HC      — reference rasterizer
Adam/Gr/SpriteBitMap.HC      — bitmap sprite subset
Adam/Gr/GrExt.HC             — extended opcodes
Doc/CharOverview.DD          — DolDoc format overview (semi-readable)
```

## Once this exists

- **BomberGolf** gets Terry's real tanks/bunkers/trees/bombs
- **Real FlapBat** and **Real Whap** become tractable
- **Talons** — the sprite decoder is one of three prereqs, but the
  smaller one. The other two (3D rasterizer, terrain generation) are
  still on the roadmap.

## Sources (external documentation)

- [DolDocOverview.DD](https://templeos.info/Wb/Doc/DolDocOverview.DD.HTML)
- [TinkerOS DolDoc docs mirror](https://tinkeros.github.io/WbTempleOS/Doc/HelpIndex.html)
- [Xe/TempleOS repository](https://github.com/Xe/TempleOS)
