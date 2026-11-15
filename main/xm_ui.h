#pragma once
/* LVGL 渲染器：调用方必须已持有 bsp_lvgl_lock()，函数内部只操作 lv_obj。 */

#include "lvgl.h"
#include "xm_vm.h"

void xm_ui_apply_locked(const xm_view_t *view, int soc);
void xm_ui_stop(void);
