#include "xm_fontcov.h"

#include <string.h>

uint8_t xm_utf8_char_len(uint8_t first) {
    if (first < 0x80u) return 1;
    if (first >= 0xC2u && first <= 0xDFu) return 2;
    if (first >= 0xE0u && first <= 0xEFu) return 3;
    if (first >= 0xF0u && first <= 0xF4u) return 4;
    return 0;
}

uint32_t xm_utf8_decode(const char **cursor) {
    if (!cursor || !*cursor || **cursor == '\0') return 0;
    const unsigned char *p = (const unsigned char *)*cursor;
    const uint8_t len = xm_utf8_char_len(p[0]);
    if (len == 0) {
        *cursor += 1;
        return 0xFFFDu;
    }
    for (uint8_t i = 1; i < len; ++i) {
        if (p[i] == 0 || (p[i] & 0xC0u) != 0x80u) {
            *cursor += i;
            return 0xFFFDu;
        }
    }
    uint32_t cp = 0;
    if (len == 1) {
        cp = p[0];
    } else if (len == 2) {
        cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    } else if (len == 3) {
        cp = ((uint32_t)(p[0] & 0x0Fu) << 12) |
             ((uint32_t)(p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    } else {
        cp = ((uint32_t)(p[0] & 0x07u) << 18) |
             ((uint32_t)(p[1] & 0x3Fu) << 12) |
             ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    }
    *cursor += len;
    return cp;
}

bool xm_font_has_glyph(const xm_fontcov_font_t *font, uint32_t codepoint) {
    if (!font || !font->codepoints || font->count == 0) return false;
    size_t lo = 0;
    size_t hi = font->count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (font->codepoints[mid] == codepoint) return true;
        if (font->codepoints[mid] < codepoint) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

xm_status_t xm_check_inventory(const xm_fontcov_font_t *font, const char *utf8_chars) {
    if (!font || !utf8_chars) return XM_ERR_INVALID_ARG;
    const char *cursor = utf8_chars;
    while (*cursor != '\0') {
        const uint32_t cp = xm_utf8_decode(&cursor);
        if (cp == 0u) continue;
        if (cp < 0x20u || cp == 0x7Fu) continue;
        if (!xm_font_has_glyph(font, cp)) return XM_ERR_TEXTS_BAD;
    }
    return XM_OK;
}
