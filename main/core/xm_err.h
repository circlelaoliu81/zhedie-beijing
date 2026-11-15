#pragma once
/* 异常态标志（E1~E5）的纯逻辑计算：不依赖 ESP-IDF / LVGL / FreeRTOS，
 * 固件 xm_app.c 与 host test 共用同一份规则，避免「只置位不清位」这类回归。 */

#include "xm_types.h"

/* 依输入条件算出本帧应显示的异常位标志。
 *
 * 语义约定（见 docs 的异常态 E-1~E-5）：
 *   - E1 时间未设定：仅当本次开机尚未由用户确认时间（xm_time_t.confirmed）时置位。
 *             用户完成授时后必须清位，否则红条会永久压在页面标题上。
 *   - E2 黄历过期：当前日期超出查表覆盖区间。
 *   - E3 存储降级：NVS 读取/格式损坏。
 *   - E4 电池不可用：不参与本函数（电量无读数时单独的位，调用方按需叠加）。
 *   - E5 内容缺失：数据表 blob 未载入。
 */
static inline uint8_t xm_err_flags_compute(bool time_confirmed,
                                           bool hl_in_range,
                                           bool store_degraded,
                                           bool content_ok) {
    uint8_t flags = 0u;
    if (!time_confirmed) flags |= XM_ERR_TIME_UNCALIBRATED_BIT;
    if (!hl_in_range) flags |= XM_ERR_HL_EXPIRED_BIT;
    if (store_degraded) flags |= XM_ERR_NVS_DEGRADED_BIT;
    if (!content_ok) flags |= XM_ERR_CONTENT_MISSING_BIT;
    return flags;
}
