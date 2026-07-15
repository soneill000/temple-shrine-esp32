// scene_fb.h — one shared off-screen framebuffer for scenes that
// composite in memory before pushing to the panel (Talons voxel
// renderer, After Egypt scenes, future TempleShim uses).
//
// Scenes never run concurrently — one game at a time — so sharing a
// single 153 KB buffer instead of allocating one per scene halves
// PSRAM pressure and keeps things linkable in constrained ESP builds.
//
// On ESP-IDF: lives in .ext_ram.bss (PSRAM) via EXT_RAM_BSS_ATTR,
// requires CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y in sdkconfig.
// On desktop / SDL: plain BSS.

#pragma once

#include <stdint.h>
#include "hw.h"

extern uint16_t g_scene_fb[SCREEN_W * SCREEN_H];
