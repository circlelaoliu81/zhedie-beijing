#pragma once
/* 中文字体声明、4 个可写 fallback 描述符与控件字体绑定表。 */

#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *control;
    uint8_t size;
    const char *symbol;
} xm_font_binding_t;

extern const xm_font_binding_t xm_font_binding_table[];
extern const size_t xm_font_binding_table_size;

void xm_fonts_init(void);
const lv_font_t *xm_fonts_font_for_size(uint8_t size);
