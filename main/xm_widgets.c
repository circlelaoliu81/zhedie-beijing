#include "xm_widgets.h"

/* 折线合并池：天师图有上百条短线段，若一条线段一个 lv_line 对象，
 * 每次重绘要创建上百个 240×320 的对象，ESP32-C3 上会明显卡顿。
 * 同色同宽且首尾相接的连续段合并成一条 lv_line 折线，对象数降到十几。
 * lv_line_set_points() 保存的是指针，所以所有折线共用这块静态池的不同区段。 */
#define XM_WIDGET_POLY_POINTS 256u
static lv_point_precise_t s_poly[XM_WIDGET_POLY_POINTS];
static uint16_t s_poly_used;

static void xm_widgets_push_point(int16_t x, int16_t y) {
    if (s_poly_used >= XM_WIDGET_POLY_POINTS) return;
    s_poly[s_poly_used].x = (lv_value_precise_t)x;
    s_poly[s_poly_used].y = (lv_value_precise_t)y;
    ++s_poly_used;
}

static void xm_widgets_draw_group(lv_obj_t *screen, const xm_view_stroke_t *arr,
                                  uint8_t n) {
    uint8_t i = 0u;
    while (i < n) {
        const xm_view_stroke_t *first = &arr[i];
        if (s_poly_used + 2u > XM_WIDGET_POLY_POINTS) return;
        const uint16_t start = s_poly_used;
        xm_widgets_push_point(first->x1, first->y1);
        xm_widgets_push_point(first->x2, first->y2);

        int16_t cx = first->x2;
        int16_t cy = first->y2;
        uint8_t j = (uint8_t)(i + 1u);
        while (j < n) {
            const xm_view_stroke_t *s = &arr[j];
            if (s->color != first->color || s->width != first->width) break;
            if (s->x1 != cx || s->y1 != cy) break;
            if (s_poly_used + 1u > XM_WIDGET_POLY_POINTS) break;
            xm_widgets_push_point(s->x2, s->y2);
            cx = s->x2;
            cy = s->y2;
            ++j;
        }

        lv_obj_t *line = lv_line_create(screen);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_set_size(line, 240, 320);
        lv_obj_set_style_line_color(line, lv_color_hex(first->color), 0);
        lv_obj_set_style_line_width(line, first->width, 0);
        lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
        lv_line_set_points(line, &s_poly[start], (uint16_t)(s_poly_used - start));
        i = j;
    }
}

/* 天师白描底图：RGB565 常量在 Flash 里，lv_image 直读，不额外占 RAM。
 * xm_view_t 只带指针，这里现造一个 lv_image_dsc_t 描述它。描述符必须长期有效
 * （lv_image_set_src 保存的是指针），所以放静态存储。每页最多一张图。 */
static lv_image_dsc_t s_img_dsc;

void xm_widgets_draw_image(lv_obj_t *screen, const xm_view_t *view) {
    if (!screen || !view || !view->image) return;
    if (view->image_w == 0u || view->image_h == 0u) return;
    s_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.flags = 0u;
    s_img_dsc.header.w = view->image_w;
    s_img_dsc.header.h = view->image_h;
    s_img_dsc.header.stride = (uint32_t)view->image_w * 2u;
    s_img_dsc.header.reserved_2 = 0u;
    s_img_dsc.data_size = (uint32_t)view->image_w * view->image_h * 2u;
    s_img_dsc.data = (const uint8_t *)view->image;
    s_img_dsc.reserved = NULL;
    s_img_dsc.reserved_2 = NULL;

    lv_obj_t *img = lv_image_create(screen);
    lv_obj_set_pos(img, view->image_x, view->image_y);
    lv_image_set_src(img, &s_img_dsc);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
}

/* 图标 sprite：Flash 里的 RGB565A8（RGB565 平面 + A8 平面）。
 *
 * 三个必须照做的点（社区已踩过，见 docs/reference/shinku-chen/
 * lossless-sprite-compositing-without-psram 与 galgame-port-lessons）：
 *   1. 每个 lv_image 必须有自己的描述符 —— lv_image_set_src 保存的是指针，
 *      共用一份会让所有图标都显示最后写入的那张。
 *   2. lv_image_set_src 会把对象尺寸重置成内容尺寸；描述符不被识别时是 0x0，
 *      对象直接塌掉，所以 set_src 之后必须再显式 set_size。
 *   3. 交给 LVGL 之前校验描述符头（magic / cf / w / h / stride）。
 *      alpha 平面 offset 由 LVGL 按 stride*h 推出，因此 stride 必须等于 w*2。 */
static lv_image_dsc_t s_icon_dsc[XM_VIEW_ICON_MAX];

void xm_widgets_draw_icons(lv_obj_t *screen, const xm_view_t *view) {
    if (!screen || !view) return;
    for (uint8_t i = 0; i < view->icon_n && i < XM_VIEW_ICON_MAX; ++i) {
        const xm_view_icon_t *ic = &view->icons[i];
        if (!ic->data || ic->w == 0u || ic->h == 0u) continue;
        lv_image_dsc_t *dsc = &s_icon_dsc[i];
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
        dsc->header.flags = 0u;
        dsc->header.w = ic->w;
        dsc->header.h = ic->h;
        dsc->header.stride = (uint32_t)ic->w * 2u;
        dsc->header.reserved_2 = 0u;
        dsc->data_size = (uint32_t)ic->w * ic->h * 3u;
        dsc->data = ic->data;
        dsc->reserved = NULL;
        dsc->reserved_2 = NULL;

        lv_obj_t *img = lv_image_create(screen);
        lv_image_set_src(img, dsc);
        lv_obj_set_pos(img, ic->x, ic->y);
        lv_obj_set_size(img, ic->w, ic->h);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void xm_widgets_draw_boxes(lv_obj_t *screen, const xm_view_t *view) {
    if (!screen || !view) return;
    /* 上限必须跟视图模型的容量走。这里曾经写死 32，而 XM_VIEW_BOX_MAX 后来
     * 提到 40（说明页/首页需要更多框），多出来的 8 个框会被静默丢掉 ——
     * host 预览照画不误，只有真机上少几块，正是"假绿"的典型。 */
    for (uint8_t i = 0; i < view->box_n && i < XM_VIEW_BOX_MAX; ++i) {
        const xm_view_box_t *box = &view->boxes[i];
        lv_obj_t *obj = lv_obj_create(screen);
        lv_obj_set_pos(obj, box->x, box->y);
        lv_obj_set_size(obj, box->w, box->h);
        lv_obj_set_style_bg_opa(obj, box->fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(box->color), 0);
        lv_obj_set_style_border_width(obj, box->border ? 1 : 0, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(box->color), 0);
        lv_obj_set_style_radius(obj, box->radius, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
}

void xm_widgets_draw_strokes(lv_obj_t *screen, const xm_view_t *view) {
    if (!screen || !view) return;
    s_poly_used = 0u;
    /* 白描底图由 xm_widgets_draw_image() 在 boxes 之前先画，这里只画
     * 卦象 / 爻线 / 铜钱等业务线段（这些必须压在底图之上）。 */
    xm_widgets_draw_group(screen, view->strokes, view->stroke_n);
}
