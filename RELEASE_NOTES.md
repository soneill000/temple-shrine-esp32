# TempleShrine v1.0.0-demo

First public release of TempleShrine — a TempleOS-flavored launcher, game collection, and Meshtastic radio broadcaster for the 2026 DEF CON badge. In memory of Terry A. Davis (1969–2018).

## Flashing

Download all three `.bin` files from this release and flash with esptool:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash \
    0x0000  bootloader.bin \
    0x8000  partitions.bin \
    0x10000 firmware.bin
```

On Windows swap `/dev/ttyACM0` for `COM3` (or whatever the badge enumerates as). If flashing fails with "Failed to connect", hold **BOOT (`GPIO_0`)**, tap **RESET**, release **BOOT** to force the ROM bootloader, then re-run.

## What's in this build

**12 launcher scenes:**

- **AFTER EGYPT** — Full port of Terry's `AfterEgypt.HC` (Talk with God, Water Rock, Battle, Break Camp, Beg for Meat, Mt Horeb ascent + 2.5D wilderness wander)
- **EAGLE DIVE** — Terry's `EagleDive` voxel flight sim with the eagle-talons swoop overlay
- **HOLYMESH** — Real Meshtastic v2 broadcaster/receiver on US LongFast primary (906.875 MHz, SF11 BW=250 CR=4/5). Compatible with any Meshtastic device in range
- **ORACLE** — Terry's F7 `GodWord`
- **HYMN** — 17 songs: 7 traditional hymns, Paranoid + Enter Sandman (Terry's picks), 8 Terry Psalmody compositions
- **SCRIPTURE** — KJV reader in DolDoc blue/white style
- **HOLYC SHELL** — Fake TempleOS terminal with CRT scanlines
- **CHRONICLE** — 9-page memorial reader
- **DIGITS**, **BOMBERGOLF**, **SQUIRT**, **BUGBIRD** — Terry's own games ported using his extracted sprites

**Radio interoperability:**
The badge broadcasts as a proper Meshtastic node named `TempleShrine` (short: `TMPL`) using AES-128-CTR encryption on the default LongFast channel. Any Meshtastic phone or device in range will see it in their Nodes list and receive/relay its text broadcasts.

## Requirements

- 2026 DEF CON badge from [RetiaLLC](https://github.com/RetiaLLC/DefconBadge2026) — ESP32-S3 + ILI9341 TFT + RFM95W LoRa + piezo + d-pad
- Python 3 with `esptool` installed (`pip install esptool`) for flashing

## Verifying the download

SHA-256 checksums:

```
cdea9b22637dfcf81e304c1a2f595c5b0428c463ccaf00e1a6fa8b56360f16b8  bootloader.bin
7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820  partitions.bin
bc53ccaef1e3892828478d7961b6899a697bb6bbe561bdecd8782f7eb6688424  firmware.bin
```

## What's next (post-demo)

- Attract / idle mode for badge-on-a-table demo use (auto-cycles Chronicle pages after ~30 s of no input)
- NVS persistence for game high scores (BomberGolf, EagleDive, BugBird)
- Adventure port (blocked on TempleOS ISO extraction)
- Merged single-bin flashing (bootloader + partitions + firmware fused into one file)
- Serial log of LoRa TX/RX for laptop-side debugging

## Credits

Terry Davis for the entire TempleOS universe (public domain). Retia LLC for the badge board + bootstrap. canewsin/templeos-1 for the TempleOS source mirror. The Meshtastic firmware + RadioLib for the protocol reference. And Sarah, who tested this on real hardware, debugged the whole radio stack alongside me, and made the calls on what belonged in the shrine.

Released under MIT.

*In memory of Terry.*
