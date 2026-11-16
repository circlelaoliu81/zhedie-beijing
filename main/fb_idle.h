#pragma once
/* 折叠北京 · 空闲息屏（纯逻辑，不碰 LVGL / FreeRTOS）
 *
 * 为什么要有这一层：固件原先背光开机就点 100%，一直亮到拔电池 —— 放在桌上
 * 忘了关，屏幕和背光就是这台小机器最大的耗电项。60 秒没按键就关背光，
 * 只是熄灯不清屏（画面还在，唤醒时不用重绘，也不会闪一下黑）。
 *
 * 唤醒策略：黑屏下的第一下按键只负责点亮，不派发给状态机。否则玩家在暗屏上
 * 盲按 OK，可能直接把角色送去「去干活」；先亮屏、再按一次才生效，符合
 * 「我在看什么」的直觉。
 *
 * 时间基准由调用方给（宿主测试用假时钟，设备用 esp_timer），本层只做状态迁移。
 */
#include <stdbool.h>
#include <stdint.h>

/* 60 秒无操作息屏 */
#define FB_IDLE_TIMEOUT_MS 60000u

typedef struct {
    uint32_t last_active_ms;   /* 最近一次按键的时刻 */
    bool asleep;               /* 背光已关 */
} fb_idle_t;

void fb_idle_init(fb_idle_t *idle, uint32_t now_ms);

/* 一次按键（按下即算，不必等抬起）。返回 true 表示这一下只用于唤醒，
 * 调用方应吞掉它，不要把按键动作交给页面状态机。 */
bool fb_idle_on_key(fb_idle_t *idle, uint32_t now_ms);

/* 轮询：越过 60 秒阈值的那一刻返回 true，只返回一次（用来关背光）。 */
bool fb_idle_poll(fb_idle_t *idle, uint32_t now_ms);

bool fb_idle_is_asleep(const fb_idle_t *idle);
