/* 折叠北京 · host 端美术符号桩。
 *
 * fb_app.c 引用的是 fb_art.h 里的链接期指针（真机上指向 Flash 里的
 * _binary_* 符号）。主机端没有那套 EMBED_FILES 符号，所以这里提供
 * 同名的空壳指针，只为了让预览工具能链接并读到正确的 w/h 与 id。
 *
 * 预览器按 image_id / icon id 去 assets/art/ 取同名 PNG 绘制真图，
 * 所以桩数据的内容无所谓 —— 但尺寸必须真实，否则布局会算错。
 */
#include <stdbool.h>
#include <stdint.h>

#include "fb_art.h"

static uint16_t s_scene_dummy[FB_SCENE_INTRO_ROOM_W * FB_SCENE_INTRO_ROOM_H];
static uint8_t s_icon_dummy[FB_ICON_SIZE * FB_ICON_SIZE * 3u];

const uint16_t *const fb_scene_intro_room = s_scene_dummy;
const uint16_t *const fb_scene_home_street = s_scene_dummy;
const uint16_t *const fb_scene_market_stall = s_scene_dummy;
const uint16_t *const fb_scene_delivery_road = s_scene_dummy;
const uint16_t *const fb_scene_gig_warehouse = s_scene_dummy;
const uint16_t *const fb_scene_ride_car = s_scene_dummy;
const uint16_t *const fb_scene_live_room = s_scene_dummy;
const uint16_t *const fb_scene_report_night = s_scene_dummy;
const uint16_t *const fb_scene_event_rain = s_scene_dummy;
const uint16_t *const fb_scene_ending_dawn = s_scene_dummy;

const uint8_t *const fb_icon_cash = s_icon_dummy;
const uint8_t *const fb_icon_heart = s_icon_dummy;
const uint8_t *const fb_icon_bolt = s_icon_dummy;
const uint8_t *const fb_icon_star = s_icon_dummy;
const uint8_t *const fb_icon_cal = s_icon_dummy;
const uint8_t *const fb_icon_work = s_icon_dummy;
const uint8_t *const fb_icon_rest = s_icon_dummy;
const uint8_t *const fb_icon_card = s_icon_dummy;
const uint8_t *const fb_icon_box = s_icon_dummy;
const uint8_t *const fb_icon_door = s_icon_dummy;
const uint8_t *const fb_icon_info = s_icon_dummy;
const uint8_t *const fb_icon_clock = s_icon_dummy;

bool fb_art_selftest(uint32_t *bad_count) {
    if (bad_count) *bad_count = 0u;
    return true;
}
