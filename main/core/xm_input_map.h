#pragma once
/* 三键事件 → 应用输入 的纯逻辑映射（无 ESP-IDF / BSP 依赖，便于 host 门禁测试）。
 *
 * 官方 docs/reference/shinku-chen/three-key-reader-interaction.zh_CN.md：
 * iot_button 会把落在短按窗口内的连按合并成 DOUBLE_CLICK（3 次以上为 MULTIPLE_CLICK），
 * 被合并的 SINGLE_CLICK 不会补发。因此连按必须当「一次普通单击」处理；
 * 把 DOUBLE 映射成另一种语义（旧版用它切授时字段 / 回首页）会让连按调数值时
 * 屏幕字段乱跳，用户描述为「键位不对、授时无法操作」。
 *
 * 长按只有确定键有全局返回语义；上/下键的长按与抬起都不产生指令。 */
#include "xm_types.h"

typedef enum {
    XM_KEY_UP = 0,
    XM_KEY_DOWN,
    XM_KEY_OK
} xm_key_t;

typedef enum {
    XM_KEY_EV_PRESS = 0,
    XM_KEY_EV_CLICK,
    XM_KEY_EV_DOUBLE,
    XM_KEY_EV_MULTIPLE,
    XM_KEY_EV_LONG,
    XM_KEY_EV_RELEASE
} xm_key_ev_t;

static inline xm_input_t xm_input_map_pure(xm_key_t key, xm_key_ev_t ev) {
    if (ev == XM_KEY_EV_CLICK || ev == XM_KEY_EV_DOUBLE ||
        ev == XM_KEY_EV_MULTIPLE) {
        if (key == XM_KEY_UP) return XM_IN_UP_CLICK;
        if (key == XM_KEY_DOWN) return XM_IN_DOWN_CLICK;
        if (key == XM_KEY_OK) return XM_IN_OK_CLICK;
    } else if (ev == XM_KEY_EV_LONG) {
        if (key == XM_KEY_OK) return XM_IN_OK_LONG;
    }
    return XM_IN_NONE;
}
