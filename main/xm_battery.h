#pragma once
/* 电量轮询：低频 LVGL timer 读取 bsp_battery_soc()，失败隐藏电量位。 */

#include "lvgl.h"

void xm_battery_attach(lv_obj_t *label);
void xm_battery_start(void);
void xm_battery_stop(void);
