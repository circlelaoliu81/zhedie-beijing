// 本文件由 tools/xm/make_fb_art.py 生成，请勿手改。
// 符号绑定与长度自检：EMBED_FILES 只产出裸符号，这里做唯一一处类型化。
#include <stdbool.h>

#include "fb_art.h"

#define BIND_SCENE(name)                                                  \
    extern const uint8_t _binary_##name##_rgb565_start[];                   \
    extern const uint8_t _binary_##name##_rgb565_end[];                     \
    const uint16_t *const fb_scene_##name =                                \
        (const uint16_t *)_binary_##name##_rgb565_start;
#define BIND_ICON(name)                                                   \
    extern const uint8_t _binary_##name##_rgb565a8_start[];                 \
    extern const uint8_t _binary_##name##_rgb565a8_end[];                   \
    const uint8_t *const fb_icon_##name =                                  \
        _binary_##name##_rgb565a8_start;

BIND_SCENE(intro_room)
BIND_SCENE(home_street)
BIND_SCENE(market_stall)
BIND_SCENE(delivery_road)
BIND_SCENE(gig_warehouse)
BIND_SCENE(ride_car)
BIND_SCENE(live_room)
BIND_SCENE(report_night)
BIND_SCENE(event_rain)
BIND_SCENE(ending_dawn)
BIND_ICON(cash)
BIND_ICON(heart)
BIND_ICON(bolt)
BIND_ICON(star)
BIND_ICON(cal)
BIND_ICON(work)
BIND_ICON(rest)
BIND_ICON(card)
BIND_ICON(box)
BIND_ICON(door)
BIND_ICON(info)
BIND_ICON(clock)

/* 资源自检：字节数与清单不一致就说明嵌入的文件和生成器对不上。
 * 这类错误在设备上表现成花屏或黑块而不是报错，所以必须主动跑一次。
 * 用 uintptr_t 相减：跨对象指针相减在标准里是 UB，链接器层面才有意义。 */
bool fb_art_selftest(uint32_t *bad_count) {
    bool ok = true;
    if (bad_count) *bad_count = 0;

    if ((uintptr_t)_binary_intro_room_rgb565_end - (uintptr_t)_binary_intro_room_rgb565_start != (uintptr_t)FB_SCENE_INTRO_ROOM_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_home_street_rgb565_end - (uintptr_t)_binary_home_street_rgb565_start != (uintptr_t)FB_SCENE_HOME_STREET_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_market_stall_rgb565_end - (uintptr_t)_binary_market_stall_rgb565_start != (uintptr_t)FB_SCENE_MARKET_STALL_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_delivery_road_rgb565_end - (uintptr_t)_binary_delivery_road_rgb565_start != (uintptr_t)FB_SCENE_DELIVERY_ROAD_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_gig_warehouse_rgb565_end - (uintptr_t)_binary_gig_warehouse_rgb565_start != (uintptr_t)FB_SCENE_GIG_WAREHOUSE_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_ride_car_rgb565_end - (uintptr_t)_binary_ride_car_rgb565_start != (uintptr_t)FB_SCENE_RIDE_CAR_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_live_room_rgb565_end - (uintptr_t)_binary_live_room_rgb565_start != (uintptr_t)FB_SCENE_LIVE_ROOM_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_report_night_rgb565_end - (uintptr_t)_binary_report_night_rgb565_start != (uintptr_t)FB_SCENE_REPORT_NIGHT_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_event_rain_rgb565_end - (uintptr_t)_binary_event_rain_rgb565_start != (uintptr_t)FB_SCENE_EVENT_RAIN_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_ending_dawn_rgb565_end - (uintptr_t)_binary_ending_dawn_rgb565_start != (uintptr_t)FB_SCENE_ENDING_DAWN_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_cash_rgb565a8_end - (uintptr_t)_binary_cash_rgb565a8_start != (uintptr_t)FB_ICON_CASH_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_heart_rgb565a8_end - (uintptr_t)_binary_heart_rgb565a8_start != (uintptr_t)FB_ICON_HEART_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_bolt_rgb565a8_end - (uintptr_t)_binary_bolt_rgb565a8_start != (uintptr_t)FB_ICON_BOLT_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_star_rgb565a8_end - (uintptr_t)_binary_star_rgb565a8_start != (uintptr_t)FB_ICON_STAR_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_cal_rgb565a8_end - (uintptr_t)_binary_cal_rgb565a8_start != (uintptr_t)FB_ICON_CAL_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_work_rgb565a8_end - (uintptr_t)_binary_work_rgb565a8_start != (uintptr_t)FB_ICON_WORK_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_rest_rgb565a8_end - (uintptr_t)_binary_rest_rgb565a8_start != (uintptr_t)FB_ICON_REST_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_card_rgb565a8_end - (uintptr_t)_binary_card_rgb565a8_start != (uintptr_t)FB_ICON_CARD_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_box_rgb565a8_end - (uintptr_t)_binary_box_rgb565a8_start != (uintptr_t)FB_ICON_BOX_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_door_rgb565a8_end - (uintptr_t)_binary_door_rgb565a8_start != (uintptr_t)FB_ICON_DOOR_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_info_rgb565a8_end - (uintptr_t)_binary_info_rgb565a8_start != (uintptr_t)FB_ICON_INFO_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    if ((uintptr_t)_binary_clock_rgb565a8_end - (uintptr_t)_binary_clock_rgb565a8_start != (uintptr_t)FB_ICON_CLOCK_BYTES) {
        ok = false;
        if (bad_count) ++(*bad_count);
    }
    return ok;
}
