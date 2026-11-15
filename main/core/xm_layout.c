#include "xm_layout.h"

#include <string.h>
#include "xm_fontcov.h"

xm_status_t xm_utf8_terminate_safe(char *buf, size_t cap) {
    if (!buf || cap == 0) return XM_ERR_INVALID_ARG;
    size_t i = 0;
    size_t last_valid = 0;
    while (i < cap && buf[i] != '\0') {
        const uint8_t first = (uint8_t)buf[i];
        const uint8_t len = xm_utf8_char_len(first);
        if (len == 0 || i + len > cap) break;
        bool ok = true;
        for (uint8_t j = 1; j < len; ++j) {
            if (((uint8_t)buf[i + j] & 0xC0u) != 0x80u) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
        i += len;
        last_valid = i;
    }
    if (last_valid >= cap) last_valid = cap - 1;
    buf[last_valid] = '\0';
    return XM_OK;
}

uint8_t xm_layout_wrap(const char *text, uint8_t font_size, uint16_t width_px) {
    if (!text || width_px == 0 || font_size == 0) return 0;
    const char *cursor = text;
    uint8_t lines = 1;
    uint16_t x = 0;
    while (*cursor != '\0') {
        const char *start = cursor;
        const uint32_t cp = xm_utf8_decode(&cursor);
        if (cp == 0xFFFDu || cp == 0u) continue;
        if (cp == '\n') {
            lines++;
            x = 0;
            continue;
        }
        const uint16_t advance = cp < 0x80u
            ? (uint16_t)((font_size / 2u) + 1u)
            : (uint16_t)font_size;
        if (x + advance > width_px) {
            lines++;
            x = advance;
        } else {
            x = (uint16_t)(x + advance);
        }
        if (lines == 255u) break;
        (void)start;
    }
    return lines;
}

uint8_t xm_layout_page_lines(const xm_view_t *view) {
    if (!view) return 0;
    return view->line_n > 24u ? 24u : view->line_n;
}

static bool xm_layout_line_zone_ok(const xm_view_line_t *line) {
    const int16_t y = line->y;
    const int16_t h = (int16_t)(line->size + 4);
    if (y < 22) return y >= 0 && y + h <= 22;
    if (y < 264) return y >= 22 && y + h <= 264;
    if (y < 288) return y >= 264 && y + h <= 288;
    return y >= 288 && y + h <= 320;
}

static bool xm_layout_box_zone_ok(const xm_view_box_t *box) {
    const int16_t y = box->y;
    const int16_t h = (int16_t)box->h;
    if (y < 22) return y >= 0 && y + h <= 22;
    if (y < 264) return y >= 22 && y + h <= 264;
    if (y < 288) return y >= 264 && y + h <= 288;
    return y >= 288 && y + h <= 320;
}

static bool xm_layout_stroke_zone_ok(const xm_view_stroke_t *stroke) {
    const int16_t min_y = stroke->y1 < stroke->y2 ? stroke->y1 : stroke->y2;
    const int16_t max_y = stroke->y1 > stroke->y2 ? stroke->y1 : stroke->y2;
    if (min_y < 22) return min_y >= 0 && max_y <= 22;
    if (min_y < 264) return max_y <= 264;
    if (min_y < 288) return max_y <= 288;
    return max_y <= 320;
}

bool xm_layout_assert_footer(const xm_view_t *view) {
    if (!view || !view->footer) return false;
    bool has_footer = false;
    bool has_keys = false;
    for (uint8_t i = 0; i < view->line_n && i < 24u; ++i) {
        const xm_view_line_t *line = &view->lines[i];
        if (!xm_layout_line_zone_ok(line)) return false;
        if (line->y >= 288) has_footer = true;
        if (line->y >= 264 && line->y < 288) has_keys = true;
    }
    for (uint8_t i = 0; i < view->box_n && i < 32u; ++i) {
        if (!xm_layout_box_zone_ok(&view->boxes[i])) return false;
    }
    for (uint8_t i = 0; i < view->stroke_n && i < 16u; ++i) {
        if (!xm_layout_stroke_zone_ok(&view->strokes[i])) return false;
    }
    /* 天师白描位图是真实上屏像素：必须完整落在内容区 22..264 内，
     * 不许压顶栏状态条、按键提示区(264..288)或页脚(288..320)。 */
    if (view->image) {
        if (view->image_w == 0u || view->image_h == 0u) return false;
        const int32_t x2 = (int32_t)view->image_x + (int32_t)view->image_w;
        const int32_t y2 = (int32_t)view->image_y + (int32_t)view->image_h;
        if (view->image_x < 0 || x2 > 240) return false;
        if (view->image_y < 22 || y2 > 264) return false;
    }
    return has_footer && has_keys;
}
