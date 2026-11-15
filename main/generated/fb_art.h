// 本文件由 tools/xm/make_fb_art.py 生成，请勿手改。
// 场景图为 RGB565（Flash 直读、零 RAM）；图标为 RGB565A8（RGB565 平面 + A8 平面）。
#pragma once

#include <stdint.h>

/* 场景图：240x104 横幅（封面/结局另用 240x320 全屏）。 */

#define FB_SCENE_INTRO_ROOM_W 240u
#define FB_SCENE_INTRO_ROOM_H 104u
#define FB_SCENE_INTRO_ROOM_BYTES 49920u
extern const uint16_t *const fb_scene_intro_room;

#define FB_SCENE_HOME_STREET_W 240u
#define FB_SCENE_HOME_STREET_H 104u
#define FB_SCENE_HOME_STREET_BYTES 49920u
extern const uint16_t *const fb_scene_home_street;

#define FB_SCENE_MARKET_STALL_W 240u
#define FB_SCENE_MARKET_STALL_H 104u
#define FB_SCENE_MARKET_STALL_BYTES 49920u
extern const uint16_t *const fb_scene_market_stall;

#define FB_SCENE_DELIVERY_ROAD_W 240u
#define FB_SCENE_DELIVERY_ROAD_H 104u
#define FB_SCENE_DELIVERY_ROAD_BYTES 49920u
extern const uint16_t *const fb_scene_delivery_road;

#define FB_SCENE_GIG_WAREHOUSE_W 240u
#define FB_SCENE_GIG_WAREHOUSE_H 104u
#define FB_SCENE_GIG_WAREHOUSE_BYTES 49920u
extern const uint16_t *const fb_scene_gig_warehouse;

#define FB_SCENE_RIDE_CAR_W 240u
#define FB_SCENE_RIDE_CAR_H 104u
#define FB_SCENE_RIDE_CAR_BYTES 49920u
extern const uint16_t *const fb_scene_ride_car;

#define FB_SCENE_LIVE_ROOM_W 240u
#define FB_SCENE_LIVE_ROOM_H 104u
#define FB_SCENE_LIVE_ROOM_BYTES 49920u
extern const uint16_t *const fb_scene_live_room;

#define FB_SCENE_REPORT_NIGHT_W 240u
#define FB_SCENE_REPORT_NIGHT_H 104u
#define FB_SCENE_REPORT_NIGHT_BYTES 49920u
extern const uint16_t *const fb_scene_report_night;

#define FB_SCENE_EVENT_RAIN_W 240u
#define FB_SCENE_EVENT_RAIN_H 104u
#define FB_SCENE_EVENT_RAIN_BYTES 49920u
extern const uint16_t *const fb_scene_event_rain;

#define FB_SCENE_ENDING_DAWN_W 240u
#define FB_SCENE_ENDING_DAWN_H 104u
#define FB_SCENE_ENDING_DAWN_BYTES 49920u
extern const uint16_t *const fb_scene_ending_dawn;

/* 图标：16x16，RGB565 平面在前、A8 平面紧随其后。 */

#define FB_ICON_SIZE 16u

#define FB_ICON_CASH_W 16u
#define FB_ICON_CASH_H 16u
#define FB_ICON_CASH_BYTES 768u
extern const uint8_t *const fb_icon_cash;

#define FB_ICON_HEART_W 16u
#define FB_ICON_HEART_H 16u
#define FB_ICON_HEART_BYTES 768u
extern const uint8_t *const fb_icon_heart;

#define FB_ICON_BOLT_W 16u
#define FB_ICON_BOLT_H 16u
#define FB_ICON_BOLT_BYTES 768u
extern const uint8_t *const fb_icon_bolt;

#define FB_ICON_STAR_W 16u
#define FB_ICON_STAR_H 16u
#define FB_ICON_STAR_BYTES 768u
extern const uint8_t *const fb_icon_star;

#define FB_ICON_CAL_W 16u
#define FB_ICON_CAL_H 16u
#define FB_ICON_CAL_BYTES 768u
extern const uint8_t *const fb_icon_cal;

#define FB_ICON_WORK_W 16u
#define FB_ICON_WORK_H 16u
#define FB_ICON_WORK_BYTES 768u
extern const uint8_t *const fb_icon_work;

#define FB_ICON_REST_W 16u
#define FB_ICON_REST_H 16u
#define FB_ICON_REST_BYTES 768u
extern const uint8_t *const fb_icon_rest;

#define FB_ICON_CARD_W 16u
#define FB_ICON_CARD_H 16u
#define FB_ICON_CARD_BYTES 768u
extern const uint8_t *const fb_icon_card;

#define FB_ICON_BOX_W 16u
#define FB_ICON_BOX_H 16u
#define FB_ICON_BOX_BYTES 768u
extern const uint8_t *const fb_icon_box;

#define FB_ICON_DOOR_W 16u
#define FB_ICON_DOOR_H 16u
#define FB_ICON_DOOR_BYTES 768u
extern const uint8_t *const fb_icon_door;

#define FB_ICON_INFO_W 16u
#define FB_ICON_INFO_H 16u
#define FB_ICON_INFO_BYTES 768u
extern const uint8_t *const fb_icon_info;

#define FB_ICON_CLOCK_W 16u
#define FB_ICON_CLOCK_H 16u
#define FB_ICON_CLOCK_BYTES 768u
extern const uint8_t *const fb_icon_clock;

/* 图标索引：视图模型只带 id，不搬指针。 */
typedef enum {
    FB_ICO_NONE = 0,
    FB_ICO_CASH,
    FB_ICO_HEART,
    FB_ICO_BOLT,
    FB_ICO_STAR,
    FB_ICO_CAL,
    FB_ICO_WORK,
    FB_ICO_REST,
    FB_ICO_CARD,
    FB_ICO_BOX,
    FB_ICO_DOOR,
    FB_ICO_INFO,
    FB_ICO_CLOCK,
    FB_ICO_COUNT
} fb_icon_id_t;

/* 嵌入长度自检：由 fb_art_data.c 实现，启动时调一次。 */
#include <stdbool.h>
bool fb_art_selftest(uint32_t *bad_count);

