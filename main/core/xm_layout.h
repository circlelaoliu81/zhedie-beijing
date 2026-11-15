#pragma once
/* 240×320 布局安全区、UTF-8 换行与页脚不可遮挡断言。 */

#include "xm_vm.h"

#define XM_LAYOUT_W 240u
#define XM_LAYOUT_H 320u
#define XM_LAYOUT_STATUS_Y 0u
#define XM_LAYOUT_STATUS_H 22u
#define XM_LAYOUT_BODY_Y 22u
#define XM_LAYOUT_BODY_H 242u
#define XM_LAYOUT_KEYS_Y 264u
#define XM_LAYOUT_KEYS_H 24u
#define XM_LAYOUT_FOOTER_Y 288u
#define XM_LAYOUT_FOOTER_H 32u
#define XM_LAYOUT_SAFE_X 8u
#define XM_LAYOUT_SAFE_W 224u

uint8_t xm_layout_wrap(const char *text, uint8_t font_size, uint16_t width_px);
uint8_t xm_layout_page_lines(const xm_view_t *view);
bool xm_layout_assert_footer(const xm_view_t *view);
xm_status_t xm_utf8_terminate_safe(char *buf, size_t cap);
