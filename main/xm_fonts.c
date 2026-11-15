#include "xm_fonts.h"

#include <string.h>

LV_FONT_DECLARE(xm_font_12);
LV_FONT_DECLARE(xm_font_16);
LV_FONT_DECLARE(xm_font_20);
LV_FONT_DECLARE(xm_font_24);
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_24);

static lv_font_t s_xm_font_with_symbols[4];

const xm_font_binding_t xm_font_binding_table[] = {
    {"status_title", 12, "xm_font_12"},
    {"status_battery", 12, "xm_font_12"},
    {"key_hint", 12, "xm_font_12"},
    {"footer_disclaimer", 12, "xm_font_12"},
    {"error_overlay", 12, "xm_font_12"},
    {"body_line", 16, "xm_font_16"},
    {"body_selected", 20, "xm_font_20"},
    {"body_disabled", 16, "xm_font_16"},
    {"page_title", 20, "xm_font_20"},
    {"main_number", 24, "xm_font_24"},
    {"hexagram_name", 24, "xm_font_24"},
    {"source_note", 12, "xm_font_12"},
    {"popup_hint", 12, "xm_font_12"}
};
const size_t xm_font_binding_table_size =
    sizeof(xm_font_binding_table) / sizeof(xm_font_binding_table[0]);

void xm_fonts_init(void) {
    s_xm_font_with_symbols[0] = xm_font_12;
    s_xm_font_with_symbols[0].fallback = &lv_font_montserrat_12;
    s_xm_font_with_symbols[1] = xm_font_16;
    s_xm_font_with_symbols[1].fallback = &lv_font_montserrat_16;
    s_xm_font_with_symbols[2] = xm_font_20;
    s_xm_font_with_symbols[2].fallback = &lv_font_montserrat_20;
    s_xm_font_with_symbols[3] = xm_font_24;
    s_xm_font_with_symbols[3].fallback = &lv_font_montserrat_24;
}

const lv_font_t *xm_fonts_font_for_size(uint8_t size) {
    uint8_t index = 1;
    if (size <= 12) index = 0;
    else if (size <= 16) index = 1;
    else if (size <= 20) index = 2;
    else index = 3;
    return &s_xm_font_with_symbols[index];
}
