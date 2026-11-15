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
#include "xm_input_map.h"
#include "xm_ui.h"

#define XM_INPUT_QUEUE_DEPTH 8
/* 与 main 任务同源：每次按键都要走完整渲染链（xm_view_t 缓冲 + printf 栈帧）。
 * 4096B 余量不足 1KB，真机深链渲染会踩栈，随 main 一并抬到 8KB。 */
#define XM_INPUT_TASK_STACK 8192
#define XM_INPUT_TASK_PRIO 5
#define XM_INPUT_TICK_MS 100

typedef struct {
    xm_input_t input;
} xm_input_msg_t;

static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static fb_app_t *s_app = NULL;
static volatile bool s_input_ready = false;
static key_guard_t s_key_guard;

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
    const xm_input_t input = xm_input_map(btn, ev);
    if (input == XM_IN_NONE || !s_input_ready || !s_input_queue) return;
    const xm_input_msg_t msg = {.input = input};
    (void)xQueueSend(s_input_queue, &msg, 0);
}

static void xm_input_loop(void *arg) {
    (void)arg;
    xm_input_msg_t msg;
    for (;;) {
        const BaseType_t got = xQueueReceive(s_input_queue, &msg,
                                             pdMS_TO_TICKS(XM_INPUT_TICK_MS));
        if (got == pdTRUE) {
            fb_app_on_key(s_app, msg.input);
            /* 状态机只在「进入新一天 / 局结束」时置 save_dirty，
             * 一天最多写两次 NVS，不用操心 Flash 磨损。 */
            app_persist_tick(s_app);
        }
        if (!fb_app_need_render(s_app)) continue;
        const int soc = bsp_battery_soc();
        xm_view_t view;
        fb_app_render(s_app, &view);
        if (view.line_n == 0u) continue;
        if (bsp_lvgl_lock(500)) {
            xm_ui_apply_locked(&view, soc);
            bsp_lvgl_unlock();
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
