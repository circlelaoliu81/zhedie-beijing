#include "xm_ui.h"

#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "xm_battery.h"
#include "xm_fonts.h"
#include "xm_status.h"
#include "xm_vm.h"
#include "xm_widgets.h"

static const char *TAG = "xm_ui";

static lv_obj_t *s_screen = NULL;

static void xm_ui_draw_lines(lv_obj_t *screen, const xm_view_t *view) {
    for (uint8_t i = 0; i < view->line_n && i < 24u; ++i) {
        const xm_view_line_t *line = &view->lines[i];
        if (!line->text) continue;
        lv_obj_t *label = lv_label_create(screen);
        lv_obj_set_pos(label, line->x, line->y);
        lv_obj_set_size(label, line->w, (int32_t)line->size + 6);
        lv_obj_set_style_text_font(label, xm_fonts_font_for_size(line->size), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(line->color), 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(label, 0, 0);
        lv_obj_set_style_pad_all(label, 0, 0);
        lv_text_align_t align = LV_TEXT_ALIGN_LEFT;
        if (line->align == XM_ALIGN_CENTER) align = LV_TEXT_ALIGN_CENTER;
        else if (line->align == XM_ALIGN_RIGHT) align = LV_TEXT_ALIGN_RIGHT;
        lv_obj_set_style_text_align(label, align, 0);
        lv_label_set_text_static(label, line->text);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void xm_ui_apply_locked(const xm_view_t *view, int soc) {
    if (!view) return;
    xm_battery_stop();
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    /* 与官方 ui_pixel_screen_create() 同构：screen 由 lv_obj_create(NULL) 创建，
     * 尺寸由 display 的 hres/vres 自动给出，不手动 lv_obj_set_size()，
     * 也不在锁内调用 lv_refr_now()（LVGL 9.5 会 pause refr_timer）。 */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(XM_COLOR_PAPER), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    xm_widgets_draw_image(s_screen, view);   /* 场景图垫在最下层 */
    xm_widgets_draw_icons(s_screen, view);   /* 图标压在场景图之上、面板之下 */
    xm_widgets_draw_boxes(s_screen, view);
    xm_widgets_draw_strokes(s_screen, view);
    xm_ui_draw_lines(s_screen, view);
    xm_status_apply(s_screen, view, soc);
    xm_battery_start();
    /* 这条日志是「整棵树重建一次」的计数凭证：正常游玩时只应在按键时出现，
     * 若空闲状态下持续刷屏，说明上层的 dirty 没有清零（曾经就是这么烧了两版）。 */
    ESP_LOGI(TAG, "视图重建: child=%u w=%d h=%d bg=0x%06x",
             (unsigned)lv_obj_get_child_cnt(s_screen),
             (int)lv_obj_get_width(s_screen), (int)lv_obj_get_height(s_screen),
             (unsigned)lv_color_to_u32(lv_obj_get_style_bg_color(s_screen, 0)));
    lv_screen_load(s_screen);
    /* 首帧交给 esp_lvgl_port 的 LVGL task 通过 lv_timer_handler() 提交，
     * 与官方 demo 的 enter_menu() 完全一致。 */
}

void xm_ui_stop(void) {
    xm_battery_stop();
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
}
