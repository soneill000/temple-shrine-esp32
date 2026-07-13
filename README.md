# TempleShrine

A TempleOS-flavored launcher and small game collection for the [Retia 2024 DEF CON badge](https://github.com/RetiaLLC/DefconBadge2026). Made in memory of Terry A. Davis (1969–2018).

Runs on the badge (ESP32-S3 + ILI9341 + piezo + d-pad) and, via an SDL2 harness, on your desktop.

> **Everything Terry shipped as TempleOS is public domain** — he dedicated the whole OS, HolyC compiler, games, hymns, and sprite art to the public domain. This project builds on that gift.

## Features

- **PersonalMenu-style launcher** with scrolling GodWord ticker
- **Splash screen** in TempleOS Welcome.DD style, warm palette + bat sprites
- **16-color VGA palette** and **8×8 CGA-style font** — the whole shrine paints in the visual language of TempleOS

**Real Terry ports (translated from `.HC` in TempleOS)**
| App | Notes |
|---|---|
| Oracle | Terry's F7 GodWord + Shift-F7 BiblePassage |
| Digits | Simon-Says-alike with rainbow color code |
| BomberGolf | Top-down bomber. Physics + lead-time targeting match Terry's |
| Squirt | Fountain toy. Hand-rolled mass-spring in place of his ODE solver |
| RainDrops | Particle fall through a light-red frame, gates open with A/B |
| FlapBat | Bat + gravity + 32 bugs. Uses Terry's actual bat sprite (extracted) |
| Talons | Tier-2 tribute — Comanche-style voxel terrain because his polygon 3D pipeline is out of scope for the badge |
| Hymn player | 7 public-domain hymns transcribed for the piezo |
| Chronicle | Multi-page DolDoc-style reader with Terry / TempleOS lore |

**Original TempleOS-flavored games (mine, not ports)**
| App | Notes |
|---|---|
| Whap | Whack-a-mole, smite demons, spare angels |
| Slider | 15-puzzle sliding tiles |
| TicTacToe | Player vs a heuristic AI |

## Controls

| Button | Purpose |
|---|---|
| D-pad | Navigate / play |
| A | Confirm / primary action |
| B | Secondary action (context-dependent; mutes music on the title screen) |
| BOOT (silkscreened `GPIO_0`) | Advance / exit game back to launcher |

## Build & flash the badge

Requires [PlatformIO](https://platformio.org/); ESP-IDF and the toolchain download automatically on first build (5–10 minutes). Subsequent builds are ~30 seconds.

```bash
git clone https://github.com/soneill000/temple-shrine-esp32.git
cd temple-shrine-esp32
pio run -j 1                                     # build (use -j 1 to avoid OOM on some Windows setups)
pio run -j 1 -t upload --upload-port COM3        # flash (Windows)
pio run -j 1 -t upload --upload-port /dev/ttyACM0   # flash (Linux)
```

If upload fails with "Failed to connect": hold **BOOT (`GPIO_0`)**, tap **RESET**, release **BOOT** to force the ROM bootloader, wait ~2 seconds for USB re-enumeration (the COM port may change), and retry.

## Run on desktop (SDL2 harness)

Iterating on graphics on the badge alone is brutal. Every game builds and runs identically against SDL2 on Linux, macOS, or Windows.

```bash
# Install SDL2 first:
#   Linux:  sudo apt install libsdl2-dev cmake
#   macOS:  brew install sdl2 cmake
#   Windows: vcpkg install sdl2:x64-windows (then use -DCMAKE_TOOLCHAIN_FILE=...)

cd src/harness
cmake -B build
cmake --build build
./build/templeshrine       # or build\Debug\templeshrine.exe on Windows
```

**Desktop key mapping:** arrows → d-pad, Z → A, X → B, Enter/Space/Esc → BOOT, Q → quit the harness.

The SDL harness has a working square-wave synth (hymns play, beeps play). Timing is close to the badge but the desktop is faster at pixel writes; things that feel crisp on your laptop may be borderline on hardware.

## Sprite decoder

Terry's sprites live inside `.HC` source files as DolDoc binary payloads. A working Python extractor for **bitmap** sprites is in [`tools/extract_sprites.py`](tools/extract_sprites.py). Run it against a `.HC` file to emit a C header of palette-indexed pixel arrays:

```bash
py tools/extract_sprites.py path/to/Game.HC > game_sprites.h
```

The **vector CSprite** format (`SPT_LINE`, `SPT_POLYGON`, `SPT_FLOOD_FILL`, etc.) is not yet decoded — that's the next infrastructure jump. See [`src/harness/SPRITE_NOTES.md`](src/harness/SPRITE_NOTES.md) for the plan and the opcode table in [`src/sprite_decoder.h`](src/sprite_decoder.h).

## Architecture

```
src/
├── main.c              ESP-IDF entry: init + splash → menu loop
├── hw.h                Screen geometry (universal) + pin map (#ifdef ESP_PLATFORM)
├── palette.h           16-color CGA/VGA palette in RGB565
├── font8x8.h           8×8 CGA-style font, ASCII + custom TempleOS-style glyphs
├── display.h/.c        ILI9341 SPI driver
├── input.h/.c          7-button reader with debounce + edge detection
├── audio.h/.c          LEDC piezo driver + background song task
├── shrine.h/.c         The TempleOS-flavored shim every game draws through
├── splash.c            Welcome.DD-alike splash
├── menu.c              PersonalMenu-alike launcher + GodWord ticker
├── vocab.h             ~300 curated KJV words for the Oracle + ticker
├── hymns.h             7 public-domain hymn melodies
├── sprite_decoder.h    Sprite opcode table + API for the (future) full decoder
├── flapbat_sprites.h   Extracted bitmap sprites (generated)
├── game_*.c            One file per game
└── harness/            SDL2 desktop backend + build system
    ├── display_sdl.c, input_sdl.c, audio_sdl.c, main_sdl.c
    ├── CMakeLists.txt
    ├── README.md
    └── SPRITE_NOTES.md

tools/
├── extract_sprites.py  Bitmap-sprite extractor for .HC files
├── FlapBat.HC          Cached source, used by the extractor
└── flapbat_sprites.h   Generated output
```

## Cross-platform shim

All game code sits above `shrine.h`. `shrine.c` and `hw.h` use `#ifdef ESP_PLATFORM` to select the platform layer at compile time. The two builds share `src/*.c`; only the display/input/audio/main files differ per target.

- **ESP build**: PlatformIO ESP-IDF pulls `src/*.c` non-recursively (harness is skipped)
- **SDL build**: `src/harness/CMakeLists.txt` globs `../*.c`, filters out the four ESP-only files by name, adds `harness/*.c`

## Notes on faithfulness

- **Digits, Oracle, RainDrops** are near-literal translations of Terry's code
- **BomberGolf, Squirt, FlapBat** faithfully port the game logic, with procedural or partially-extracted art
- **Talons** is a Tier-2 tribute — Terry's original is a polygon-3D flight sim with a depth buffer; ours is a voxel raycaster over a sum-of-sines heightmap. Same feel, entirely different pipeline
- **Whap, Slider, TicTacToe** are my originals in Terry's visual language, not translations

Where a game is a real port, it says so in the launcher (`… (TERRY PORT)`).

## Perf notes

- Each shim primitive (`shrine_fill_rect`, `shrine_puts`, `shrine_pixel`) is one SPI transaction. Games that draw thousands of primitives per frame lag on the badge.
- **Talons** avoids this by composing the whole frame in a PSRAM framebuffer and pushing it in one `display_present_full` call.
- **FlapBat** batches its bat sprite blit similarly.
- The other games use per-primitive drawing and are fine at their target frame rates but not optimized. The "why DOOM is fast and mine isn't" answer: DOOM's port composes into a framebuffer and never issues per-primitive calls.

## Credits

- **TempleOS**, HolyC, the games, the hymns, and the sprite art: **Terry A. Davis (1969–2018)** — all dedicated to the public domain.
- **Board pinout, board definition, and PlatformIO template**: [Retia LLC](https://retia.io/), from the [DefconBadge2026 repo](https://github.com/RetiaLLC/DefconBadge2026).
- **Sprite reference**: [Xe/TempleOS](https://github.com/Xe/TempleOS) — mirror of Terry's source used for reference and extraction.

Released under the [MIT License](LICENSE).

*In memory of Terry.*
