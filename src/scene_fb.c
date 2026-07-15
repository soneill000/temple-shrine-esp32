// scene_fb.c — definition of the shared scene framebuffer. See scene_fb.h.

#include "scene_fb.h"

#ifdef ESP_PLATFORM
  #include "esp_attr.h"
  EXT_RAM_BSS_ATTR uint16_t g_scene_fb[SCREEN_W * SCREEN_H];
#else
  uint16_t g_scene_fb[SCREEN_W * SCREEN_H];
#endif
