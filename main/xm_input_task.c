#include "xm_input_task.h"

#include <string.h>
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "key_guard.h"
#include "app_persist.h"
#include "fb_idle.h"
#include "xm_input_map.h"
#include "xm_ui.h"

#define XM_INPUT_QUEUE_DEPTH 8
/* 正常亮度。设备只有一档，息屏就是直接拉 0 —— 面板仍保留画面，
 * 唤醒不用重绘，也不会先闪一下黑底再出图。 */
#define XM_BL_ON 100
/* 与 main 任务同源：每次按键都要走完整渲染链（xm_view_t 缓冲 + printf 栈帧）。
 * 4096B 余量不足 1KB，真机深链渲染会踩栈，随 main 一并抬到 8KB。 */
#define XM_INPUT_TASK_STACK 8192
#define XM_INPUT_TASK_PRIO 5
#define XM_INPUT_TICK_MS 100

typedef struct {
    xm_input_t input;
    bool wake;              /* 只负责点亮背光，不派发给页面状态机 */
} xm_input_msg_t;

static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static fb_app_t *s_app = NULL;
static volatile bool s_input_ready = false;
static key_guard_t s_key_guard;
/* 空闲息屏状态只由输入任务写；s_asleep 是它发布给按键回调的只读快照
 * （回调跑在 esp_timer 任务里，不能反过来改状态机）。 */
static fb_idle_t s_idle;
static volatile bool s_asleep = false;
static bool s_bl_off = false;

static uint32_t xm_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void xm_backlight_apply(void) {
    const bool want_off = fb_idle_is_asleep(&s_idle);
    s_asleep = want_off;
    if (want_off == s_bl_off) return;
    s_bl_off = want_off;
    bsp_display_backlight(want_off ? 0 : XM_BL_ON);
}

/* BSP 枚举 → 纯逻辑映射（见 xm_input_map.h，规则由 host 门禁逐条守住）。
 * 连按（DOUBLE / MULTIPLE）一律当「一次普通单击」，屏上写什么键就是什么键。 */
static xm_input_t xm_input_map(bsp_btn_t btn, bsp_btn_ev_t ev) {
    xm_key_t key;
    if (btn == BSP_BTN_UP) key = XM_KEY_UP;
    else if (btn == BSP_BTN_DOWN) key = XM_KEY_DOWN;
    else if (btn == BSP_BTN_OK) key = XM_KEY_OK;
    else return XM_IN_NONE;

    xm_key_ev_t kev = XM_KEY_EV_PRESS;
    switch (ev) {
        case BSP_BTN_CLICK:   kev = XM_KEY_EV_CLICK;    break;
        case BSP_BTN_DOUBLE:  kev = XM_KEY_EV_DOUBLE;   break;
        case BSP_BTN_LONG:    kev = XM_KEY_EV_LONG;     break;
        case BSP_BTN_RELEASE: kev = XM_KEY_EV_RELEASE;  break;
        case BSP_BTN_PRESS:   kev = XM_KEY_EV_PRESS;    break;
        default:              kev = XM_KEY_EV_PRESS;    break;
    }
    return xm_input_map_pure(key, kev);
}

void xm_input_on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) return;
    if (!s_input_ready || !s_input_queue) return;
    /* 熄屏时按下任何一颗键都只点亮屏幕。这一下不进状态机：
     * 暗屏上盲按 OK 不该把角色直接送去干活。 */
    if (s_asleep) {
        const xm_input_msg_t wake = {.input = XM_IN_NONE, .wake = true};
        (void)xQueueSend(s_input_queue, &wake, 0);
        return;
    }
    const xm_input_t input = xm_input_map(btn, ev);
    if (input == XM_IN_NONE) return;
    const xm_input_msg_t msg = {.input = input, .wake = false};
    (void)xQueueSend(s_input_queue, &msg, 0);
}

static void xm_input_loop(void *arg) {
    (void)arg;
    xm_input_msg_t msg;
    fb_idle_init(&s_idle, xm_now_ms());
    for (;;) {
        const BaseType_t got = xQueueReceive(s_input_queue, &msg,
                                             pdMS_TO_TICKS(XM_INPUT_TICK_MS));
        const uint32_t now_ms = xm_now_ms();
        bool swallow = false;
        if (got == pdTRUE) {
            if (msg.wake) {
                /* 唤醒那一下由这里消费掉；状态迁移放在这，不放在回调里 */
                swallow = true;
                (void)fb_idle_on_key(&s_idle, now_ms);
            } else {
                swallow = fb_idle_on_key(&s_idle, now_ms);
            }
            if (!swallow) {
                fb_app_on_key(s_app, msg.input);
                /* 状态机只在「进入新一天 / 局结束」时置 save_dirty，
                 * 一天最多写两次 NVS，不用操心 Flash 磨损。 */
                app_persist_tick(s_app);
            }
        } else {
            /* 队列超时就是闲着的这 100ms：跨越 60 秒阈值时关背光 */
            (void)fb_idle_poll(&s_idle, now_ms);
        }
        xm_backlight_apply();
        if (s_bl_off) continue;   /* 熄屏期间不重建对象树，dirty 留着，亮屏再画 */
        if (!fb_app_need_render(s_app)) continue;
        const int soc = bsp_battery_soc();
        xm_view_t view;
        fb_app_render(s_app, &view);
        if (view.line_n == 0u) continue;
        if (bsp_lvgl_lock(500)) {
            xm_ui_apply_locked(&view, soc);
            bsp_lvgl_unlock();
            /* 只有真画上屏了才清 dirty。抢不到锁就留着，下一轮重来 ——
             * 之前这里忘了清，dirty 一旦置位就永真，整棵对象树每 100ms
             * 被销毁重建一次：屏幕闪、CPU 满。 */
            fb_app_clear_dirty(s_app);
        } else {
            fb_app_mark_dirty(s_app);
        }
    }
}

esp_err_t xm_input_task_start(fb_app_t *app) {
    if (!app) return ESP_ERR_INVALID_ARG;
    s_app = app;
    s_input_queue = xQueueCreate(XM_INPUT_QUEUE_DEPTH, sizeof(xm_input_msg_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(xm_input_loop, "xm_input", XM_INPUT_TASK_STACK, NULL,
                    XM_INPUT_TASK_PRIO, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = bsp_button_init(xm_input_on_key, NULL);
    if (err != ESP_OK) {
        xm_input_task_stop();
        return err;
    }
    s_input_ready = true;
    return ESP_OK;
}

void xm_input_task_stop(void) {
    s_input_ready = false;
    if (s_input_task) {
        vTaskDelete(s_input_task);
        s_input_task = NULL;
    }
    if (s_input_queue) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
    }
    s_app = NULL;
}
