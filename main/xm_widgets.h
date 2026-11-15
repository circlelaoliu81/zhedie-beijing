#pragma once
/* 专用绘制件：爻线、九宫格边框、朱砂提示条；全部由 xm_view_t 驱动。 */

#include "lvgl.h"
#include "xm_vm.h"

void xm_widgets_draw_image(lv_obj_t *screen, const xm_view_t *view);
void xm_widgets_draw_icons(lv_obj_t *screen, const xm_view_t *view);
void xm_widgets_draw_boxes(lv_obj_t *screen, const xm_view_t *view);
void xm_widgets_draw_strokes(lv_obj_t *screen, const xm_view_t *view);
