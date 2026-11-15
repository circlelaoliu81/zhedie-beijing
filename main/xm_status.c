#include "xm_status.h"

#include <stdio.h>
#include <string.h>
#include "xm_battery.h"
#include "xm_fonts.h"
#include "xm_vm.h"

void xm_status_apply(lv_obj_t *screen, const xm_view_t *view, int soc) {
    if (!screen || !view) return;
    lv_obj_t *battery = lv_label_create(screen);
    lv_obj_set_pos(battery, 184, 4);
    lv_obj_set_size(battery, 48, 16);
    lv_obj_set_style_text_font(battery, xm_fonts_font_for_size(12), 0);
    lv_obj_set_style_text_color(battery, lv_color_hex(XM_COLOR_INK), 0);
    lv_obj_set_style_text_align(battery, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_bg_opa(battery, LV_OPA_TRANSP, 0);
    char soc_text[16] = "";
    (void)snprintf(soc_text, sizeof(soc_text), "%d%%", soc);
    lv_label_set_text(battery, soc_text);
    if (soc < 0) lv_obj_add_flag(battery, LV_OBJ_FLAG_HIDDEN);
    xm_battery_attach(battery);

    /* 异常条画在顶栏状态区（0..22）内，与右侧电量并排：
     * 早先画在 (8,24)-(232,44)，把 7 个模板的页面大标题整行盖掉
     * （HOME/HL 三页/LY_CAST/LY_HISTORY/SETTINGS/ABOUT 的标题都在 y=30~34）。
     * 最长的一条是 E1「时间未设定：请先设定设备时间」= 168px，标签宽 172px 可容；
     * tests/test_xm_vm_layout.c 里逐条宽断言守住这一点。 */
    const char *overlay = xm_vm_overlay_text(view->overlay);
    if (overlay && overlay[0] != '\0') {
        lv_obj_t *bar = lv_obj_create(screen);
        lv_obj_set_pos(bar, 8, 2);
        lv_obj_set_size(bar, 176, 18);
        lv_obj_set_style_bg_color(bar, lv_color_hex(XM_COLOR_RED), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label = lv_label_create(bar);
        lv_obj_set_pos(label, 2, 2);
        lv_obj_set_size(label, 172, 14);
        lv_obj_set_style_text_font(label, xm_fonts_font_for_size(12), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(XM_COLOR_PAPER), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text_static(label, overlay);
    }
}
