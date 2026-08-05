# TempleShrine

A TempleOS-flavored launcher, game collection, and Meshtastic radio broadcaster for the [2026 DEF CON badge](https://github.com/RetiaLLC/DefconBadge2026). Made in memory of Terry A. Davis (1969–2018).

Runs on the badge (ESP32-S3 + ILI9341 + RFM95W LoRa + piezo + d-pad) and, via an SDL2 harness, on your desktop.

> **Everything Terry shipped as TempleOS is public domain** — he dedicated the whole OS, HolyC compiler, games, hymns, and sprite art to the public domain. This project builds on that gift.

## What's on the badge

- **PersonalMenu-style launcher** with a scrolling GodWord ticker
- **Splash screen** in TempleOS `Welcome.DD` style, warm palette + Terry's own sprites
- **16-color VGA palette** + **8×8 CGA-style font** throughout — the whole shrine paints in TempleOS visual language
- **HOLYMESH** — real Meshtastic v2 broadcaster/receiver on the US LongFast primary channel (906.875 MHz, SF11, BW=250, CR=4/5). Compatible with any Meshtastic phone or device in range: they see the badge as a `TempleShrine`/`TMPL` node and can send/receive text messages with it.

### Scenes

| Scene | What it is |
|---|---|
| **AFTER EGYPT** | Full port of Terry's `AfterEgypt.HC` — GodTalking, Water Rock, Battle, Break Camp, Beg for Meat (Quail), Mt Horeb ascent + 2.5D wilderness wander with the Burning Bush |
| **EAGLE DIVE** | Terry's `EagleDive` — voxel raycaster over his heightmap with fish spawning + on-demand eagle-talons swoop overlay |
| **HOLYMESH** | Meshtastic LoRa: random-GodWord compose mode, Terry-aphorism broadcast mode, inbox with named senders, live node scanner. Auto-announces via `NODEINFO_APP` every 120 s |
| **ORACLE** | Terry's F7 `GodWord` and Shift-F7 `GodBiblePassage` |
| **HYMN** | 17 songs on the piezo: 7 public-domain hymns, "Paranoid" + "Enter Sandman" (Terry's picks), and 8 of Terry's own Psalmody compositions parsed straight from `canewsin/templeos-1` |
| **SCRIPTURE** | KJV reader in DolDoc blue/white style — book index → passage view |
| **HOLYC SHELL** | Fake TempleOS terminal (CRT scanlines + monitor flash effect), curated command palette: `God;`, `AdamBomb;`, `PopUpOk`, `Type("Bible.TXT")` — `God;` fires a live GodWord |
| **CHRONICLE** | 9-page DolDoc reader: Terry biography, TempleOS, HolyC, the Third Temple, full "On Reality" bird-and-monitor quote, memorial |
| **DIGITS** | Terry's `Digits` — Simon-Says with rainbow color code |
| **BOMBERGOLF** | Terry's top-down bomber using his own 7 real sprites |
| **SQUIRT** | Terry's fountain physics toy, hand-rolled mass-spring in place of his ODE solver |
| **BUGBIRD** | Terry's `bugbird.cpp.z` — bird flapping through 32 bugs, using his own 4 vector sprites |

## Controls

| Button | Purpose |
|---|---|
| D-pad | Navigate / play (scene-specific) |
| A | Confirm / primary action |
| B | Secondary action (mute toggle on the launcher) |
| BOOT (silkscreened `GPIO_0`) | Exit scene back to launcher |

## Flashing a pre-built binary

Download the three `.bin` files from the [latest release](https://github.com/soneill000/temple-shrine-esp32/releases/latest) and flash with esptool:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash \
    0x0000  bootloader.bin \
    0x8000  partitions.bin \
    0x10000 firmware.bin
```

On Windows use `COM3` (or whatever `esptool.py chip_id` reports). If flashing fails with "Failed to connect," hold **BOOT (`GPIO_0`)**, tap **RESET**, release **BOOT** to force the ROM bootloader, then re-run esptool.

## Building from source

Requires [PlatformIO](https://platformio.org/). ESP-IDF and the toolchain download automatically on first build (5–10 min). Subsequent builds are ~30 s.

```bash
git clone https://github.com/soneill000/temple-shrine-esp32.git
cd temple-shrine-esp32
pio run -j 1                                        # build (-j 1 avoids OOM on some setups)
pio run -j 1 -t upload --upload-port COM3           # flash (Windows)
pio run -j 1 -t upload --upload-port /dev/ttyACM0   # flash (Linux/macOS)
```

## HOLYMESH: what it actually does on air

HOLYMESH transmits real Meshtastic v2 mesh packets — not a proprietary format. Any Meshtastic device in range on the primary channel will see the badge appear in its Nodes list as `TempleShrine` (short name `TMPL`) and receive/relay text broadcasts from it.

Under the hood:
- 16-byte Meshtastic on-air header (to, from, id, flags, channel_hash=0x08, next_hop, relay_node)
- Payload is `Data { portnum = TEXT_MESSAGE_APP, payload = <text>, want_response = false }`
- Encrypted with AES-128-CTR using the default LongFast PSK; nonce layout matches Meshtastic's `CryptoEngine::initNonce` byte-for-byte (`packet_id` at bytes 0–7 as `uint64_t` LE, `from_node` at bytes 8–11 as `uint32_t` LE)
- AES-128 is embedded from a public-domain reference impl (no `mbedtls` dependency)
- On entry the badge broadcasts a `NODEINFO_APP` frame with `long_name="TempleShrine"`, `short_name="TMPL"`, `hw_model=PRIVATE_HW` so Meshtastic clients get a User record and don't hide our text as "unknown sender"
- Node ID derived from the ESP-S3 factory MAC (lower 32 bits, Meshtastic convention)

Compose mode auto-generates a random 3–6 GodWord sequence with a `GOD SAYS:` prefix. Browse mode cycles ten verified verbatim Terry quotes ([full list in `PHRASES.md`](PHRASES.md)). Scan mode shows a live table of every Meshtastic node the badge has heard, with RSSI, channel hash, decrypted name, and "on our channel" indicator.

## Run on desktop (SDL2 harness)

Every scene runs identically on Linux/macOS/Windows via SDL2 (no radio hardware, but everything else works).

```bash
# Install SDL2 first:
#   Linux:   sudo apt install libsdl2-dev cmake
#   macOS:   brew install sdl2 cmake
#   Windows: vcpkg install sdl2:x64-windows
cd src/harness
cmake -B build
cmake --build build
./build/templeshrine
```

**Desktop key mapping:** arrows → d-pad, Z → A, X → B, Enter/Space/Esc → BOOT, Q → quit.

## Faithfulness policy

Ports labeled as Terry's work are exactly that — either direct HolyC → C translations line-by-line, or scenes rendered using his extracted sprites and mechanics.

- **Sprites** live inside `.HC` files as DolDoc binary payloads. `tools/extract_sprite_tail.py` (and the aiwnios variant) extract them into C headers ready to feed into the `templeshim` `Sprite3()` API.
- **Terry quotes** in `terry_quotes.h` are verbatim-only. Vetted sources: Terry's own vlog notes (user-supplied), Wikiquote's transcribed lines, TempleOS docs. Composed / speculative "Terry-style" phrasings were removed early in the project.
- **HolyC scene logic** is ported line-for-line where Terry's original is knowable, preserving his exact constants (`HACK_PERIOD=0.25`, `DOWN_TIME=0.075`, palette indices, y-anchor offsets). Where the original is not knowable (comic panel art, splash art if the `.DD.Z` files can't be extracted), the code is annotated inline as homage.
- **Music** — Terry's Psalmody songs are parsed straight from `canewsin/templeos-1/iso/apps/psalmody/examples/*.cpp.z` via `tools/parse_terry_play.py`, which reads his `Play()` notation and emits `note_t[]` arrays transposed +2 octaves for the badge piezo's audible band.

## Architecture

```
src/
├── main.c                 ESP-IDF entry: init + splash → menu loop
├── hw.h                   Screen geometry (universal) + pin map (ESP-only)
├── palette.h              16-color CGA/VGA palette in RGB565
├── font8x8.h              8×8 CGA-style font
├── display.h/.c           ILI9341 SPI driver
├── input.h/.c             7-button reader with debounce + edge detection
├── audio.h/.c             LEDC piezo driver + background song task
├── shrine.h/.c            The TempleOS-flavored shim every scene draws through
├── templeshim.h/.c        Terry's Gr* API (GrLine, Sprite3, ...) on our fb
├── scene_fb.h             Shared 320×240 PSRAM framebuffer for scene composition
├── splash.c               Welcome.DD-alike splash
├── menu.c                 Launcher + GodWord ticker
├── vocab.h                ~300 curated KJV words for Oracle + compose
├── terry_quotes.h         10 verbatim Terry quotes for HOLYMESH browse mode
├── hymns.h                7 public-domain hymns + 2 Terry picks
├── songs.h                8 Terry Psalmody songs (auto-gen by parse_terry_play.py)
├── lora_radio.h/.c        SX1276 driver, Meshtastic-compatible init
├── meshtastic_frame.h/.c  Meshtastic v2 frame builder/parser + embedded AES-128
├── sprite_*.h             Extracted Terry sprites (BugBird, BomberGolf, Camp, ...)
├── game_*.c               One file per scene
└── harness/               SDL2 desktop backend

tools/
├── extract_sprite_tail.py         Terry-format sprite tail extractor
├── extract_sprite_tail_aiwnios.py Aiwnios-format variant
├── parse_terry_play.py            Terry Play() notation → C note_t[]
├── AfterEgypt/                    Vendored HolyC source for scene ports
├── vendored/                      Vendored sprite sources
└── psalmody/                      Vendored Terry Psalmody song files
```

## Credits

- **TempleOS**, HolyC, the games, the hymns, and the sprite art: **Terry A. Davis (1969–2018)** — all dedicated to the public domain.
- **Board pinout, board definition, PlatformIO template**: [Retia LLC](https://retia.io/), from the [DefconBadge2026 repo](https://github.com/RetiaLLC/DefconBadge2026).
- **Sprite / source reference**: [canewsin/templeos-1](https://github.com/Canewsin/templeos-1) — mirror of Terry's source used for extraction of BugBird, BomberGolf, EagleDive, AESplash, Camp, Mountain, GodTalking, HorebA, Quail, WaterRock sprites and all 8 Psalmody songs.
- **Meshtastic protocol**: reverse-engineered from the [Meshtastic firmware source](https://github.com/meshtastic/firmware) (`CryptoEngine.cpp`, `RadioLibInterface.cpp`, `RF95Interface.cpp`) and [RadioLib SX127x driver](https://github.com/jgromes/RadioLib) — no code copied, only protocol constants and register-write sequences studied.

Released under the [MIT License](LICENSE).

*In memory of Terry.*
