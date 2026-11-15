#pragma once
/* 状态条与异常态提示：电量 soc==-1 隐藏，E-1~E-5 用朱砂提示条叠加。 */

#include "lvgl.h"
#include "xm_vm.h"

void xm_status_apply(lv_obj_t *screen, const xm_view_t *view, int soc);
