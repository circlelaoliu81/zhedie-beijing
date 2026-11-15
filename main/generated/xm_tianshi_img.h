#pragma once
/* 天师白描位图：LVGL lv_image 直读 Flash 的 RGB565 常量。
 * 像素源与 host 预览同源，由 make_tianshi_bitmap.py 保证一致。 */
#include <stdint.h>

#define TIANSHI_MAIN_W 200u
#define TIANSHI_MAIN_H 132u
extern const uint16_t tianshi_main_rgb565[TIANSHI_MAIN_W * TIANSHI_MAIN_H];

#define TIANSHI_HOME_W 160u
#define TIANSHI_HOME_H 94u
extern const uint16_t tianshi_home_rgb565[TIANSHI_HOME_W * TIANSHI_HOME_H];
