# TempleShrine port principles

This file lists the rules I need to keep straight while porting Terry Davis's
work to the badge. Read every session before writing scene code.

## Golden rule

**Terry's content is the source of truth.** When a scene, sprite, comic, or
splash file exists in Terry's original, port that. Never substitute my own
composition when the original is knowable — and when the original is *not*
knowable to me, STOP AND ASK, don't fill the gap with invention.

## What counts as literal

- Sprites/bitmaps extracted from `.HC` or `.HC.Z` DolDoc tails via
  `tools/extract_sprite_tail.py`.
- HolyC scene logic ported line-for-line into C (button semantics, math,
  timing constants, colors, text). Keep Terry's constants exact
  (`HACK_PERIOD=0.25`, `DOWN_TIME=0.075`, palette indices, y-anchor offsets).
- Terry's own labels ("Show Mercy / Punish / Really Punish", "Beg for Meat",
  "Find the Burning Bush", etc.).
- Terry's own verse citations (Exodus 14:19-20, Numbers 11:11) rendered from
  real KJV text in `src/bible.c`.

## What is NOT literal (and needs a big disclaimer)

- **Story Comics scene.** Terry's `ViewComics` reads `Comics/*.DD.Z` files
  that we don't have (need TempleOS ISO extraction + LZW). Our current
  panels are homage compositions using extracted sprites. First panel should
  clearly say "homage panels, not Terry's originals."
- **Splash / Trailer.** Terry's `Trailer()` shows `AESplash.DD.Z`. We don't
  have that either — the four TMsg lines *are* Terry's exact strings, but
  the backdrop art behind them is our composition.
- **Anything using Mountain.HC.Z**: Camp backdrop, GodTalking backdrop,
  Quail backdrop, Break Camp mountain sprite. Blocked until user extracts
  `Mountain.HC.Z` from an ISO.

## Blocked-on-ISO tracker

| File | Used by | Status |
|---|---|---|
| `Mountain.HC.Z` | Clouds, Camp, GodTalking, Quail, Squirt | not extractable from any public repo |
| `AESplash.DD.Z` | Trailer intro | not extractable |
| `Comics/*.DD.Z` | ViewComics | not extractable |
| `HSNotes.DD.Z` | AfterEgypt Help menu | not extractable |

## Rules I've broken and fixed once

- **Inventing mechanics.** Original scene_battle was a rhythm timing game.
  Terry's Battle is "hold SPACE, watch line drift" only. Port the literal
  mechanic; add badge-specific interaction ONLY when Terry's version has
  zero interactivity and the user explicitly asks for it. Even then,
  disclaim in the code comment.
- **Making up comic panels.** Six then eleven panels — none are Terry's.
  Keep the viewer for now with a first-panel disclaimer.
- **Making up backdrops.** Splash backdrop, comic panel backgrounds.
  Whenever we're compositing our own artwork, note it inline as homage.
- **Off-by-unit bugs.** Quail dt subtracted ms from seconds. Horeb horizon
  math used wrong trig identity. Whenever there's a Terry constant, sanity-
  check by tracing values with actual view coordinates.

## Chained scene flows in Terry's After Egypt

Terry's menu items sometimes chain multiple functions:
- `T_TALK_WITH_GOD` → `UpTheMountain()` → `Mountain(); Horeb();` then dialog
- Break Camp is *the pause between menu selections* in Terry, not its own
  scene — the menu re-renders while the camp draws. Our discrete scenes
  don't have to match this, but the flow matters when picking mechanics.

Read `tools/AfterEgypt/AfterEgypt.HC`'s `TakeTurn()` before porting a new
scene to catch any chained flow I might miss.

## Coordinate + timing conventions

- Terry-space is 640×480. Shim halves to 320×240 fb via `dc->scale=2`.
- `GrPrint` glyphs are 8 fb pixels wide → 16 Terry pixels per char.
- `SPT_BITMAP` row stride is `(w + 7) & ~7` — TempleOS aligns rows to 8
  bytes. Missing this scrambles the figure.
- Timing: use `shrine_ms()` for absolute, `(now - t_last) / 1000.0f` for dt
  in seconds. Never subtract ms from seconds.

## Consultation protocol

When about to invent (because Terry's source isn't available), STOP and:
1. Note the blocker (which file, which repo, etc.)
2. Propose 2-3 options (invent stand-in, remove scene, wait for extraction)
3. Wait for user's answer before writing any code
