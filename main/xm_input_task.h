#pragma once
/* 三键输入队列与任务：回调只入队，LVGL 操作持 bsp_lvgl_lock。 */

#include "bsp_button.h"
#include "esp_err.h"
#include "xm_app.h"

esp_err_t xm_input_task_start(xm_app_t *app);
void xm_input_task_stop(void);
void xm_input_on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user);
