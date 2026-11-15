#pragma once
/* UTF-8 解码与字形覆盖纯逻辑：host test 用清单数组作字体桩。 */

#include "xm_types.h"

typedef struct {
    const uint32_t *codepoints;
    size_t count;
} xm_fontcov_font_t;

uint8_t xm_utf8_char_len(uint8_t first);
uint32_t xm_utf8_decode(const char **cursor);
bool xm_font_has_glyph(const xm_fontcov_font_t *font, uint32_t codepoint);
xm_status_t xm_check_inventory(const xm_fontcov_font_t *font, const char *utf8_chars);
