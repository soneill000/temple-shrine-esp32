# TempleShrine — SDL desktop harness

Runs the whole shrine on your desktop against an SDL2 backend. Same shim,
same game code as the badge — only `display_sdl.c`, `input_sdl.c`,
`audio_sdl.c`, and `main_sdl.c` differ from the ESP-IDF build. Use this to
iterate on games and graphics without flashing.

## Controls

| Desktop key | Badge button |
|---|---|
| Arrow keys | LEFT / UP / DOWN / RIGHT |
| Z | A |
| X | B |
| Enter / Space / Esc | BOOT (start, quit-to-menu) |
| Q | Quit the harness |

## Build

Requires **CMake 3.16+**, a **C11 compiler**, and **SDL2**.

### Install SDL2

- **Linux (Debian/Ubuntu):** `sudo apt install libsdl2-dev cmake`
- **macOS (Homebrew):** `brew install sdl2 cmake`
- **Windows:** easiest via [vcpkg](https://vcpkg.io):
  ```powershell
  vcpkg install sdl2:x64-windows
  ```
  then configure with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.
  Alternatively, download the SDL2 development libraries from
  [libsdl.org](https://libsdl.org) and point `SDL2_DIR` at them.

### Configure and build

From this directory (`firmware/templeshrine/src/harness/`):

```bash
cmake -B build
cmake --build build
```

### Run

```bash
./build/templeshrine          # Linux/macOS
build\Debug\templeshrine.exe  # Windows / MSVC default
```

Window opens at 320×240 logical (960×720 physical, 3× zoom).

## Audio

**Silent stub for now.** Beeps and song events print to stderr instead of
producing sound. A real SDL_QueueAudio-based square-wave synth is on the
list — but not needed for graphics iteration, which is the main reason
this harness exists.

## What the harness IS

- A fast iteration loop for graphics, layout, palette, timing, animations
- A place to prototype 3D rasterizers (Talons) and sprite decoders
- A way to demo the shrine without hardware

## What the harness ISN'T

- A pixel-accurate emulator. Timing is close but not perfect. The badge's
  SPI-limited fill rate is faster than one might expect and slower than
  memory writes here. Rendering that feels crisp on desktop may be borderline
  on hardware.
- Byte-accurate audio. Silent stub.
- A substitute for real flashing at ship time.

## Code layout

```
firmware/templeshrine/
├── src/
│   ├── ...all shared game/shim code...
│   ├── display.c     ESP-only (ILI9341 SPI)  — excluded from SDL build
│   ├── input.c       ESP-only (GPIO)         — excluded from SDL build
│   ├── audio.c       ESP-only (LEDC + tasks) — excluded from SDL build
│   ├── main.c        ESP-only (app_main)     — excluded from SDL build
│   └── harness/      ⇐ this directory
│       ├── display_sdl.c
│       ├── input_sdl.c
│       ├── audio_sdl.c
│       ├── main_sdl.c
│       ├── CMakeLists.txt
│       └── README.md
```

The ESP-IDF build (`platformio.ini` at the repo root) uses a non-recursive
glob of `src/*.c`, so it doesn't pick up anything under `src/harness/`.
The SDL build here does the reverse: it globs `../*.c`, filters out the
four ESP-only files by name, and adds `*.c` from this directory.
