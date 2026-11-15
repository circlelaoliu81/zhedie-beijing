#include "xm_yijing.h"

#include <stdio.h>
#include <string.h>
#include "xm_yijing_data.h"

static bool s_yj_ready = false;

xm_status_t xm_yijing_init(void) {
    if (memcmp(xm_yijing_magic, XM_MAGIC_YJ, 4) != 0) return XM_ERR_TEXTS_BAD;
    if (xm_yijing_version != XM_FORMAT_VERSION) return XM_ERR_TEXTS_BAD;
    for (uint8_t i = 0; i < 64; ++i) {
        if (XM_GUA_IDX(xm_yijing[i].mark) != i) return XM_ERR_TEXTS_BAD;
    }
    s_yj_ready = true;
    return XM_OK;
}

const char *xm_gua_name(uint8_t idx) {
    return xm_gua_text(idx, XM_YJ_NAME);
}

const char *xm_gua_text(uint8_t idx, uint8_t field) {
    if (!s_yj_ready || idx >= 64 || field >= XM_YJ_FIELD_COUNT) return NULL;
    const xm_gua_entry_t *entry = &xm_yijing[idx];
    const uint32_t off = entry->off[field];
    const uint32_t len = entry->len[field];
    if (len == 0 || off + len + 1 > xm_yijing_strpool_size) return NULL;
    const char *str = (const char *)(xm_yijing_strpool + off);
    if (str[len] != 0) return NULL;
    return str;
}

uint8_t xm_gua_mark(uint8_t idx) {
    if (idx >= 64) return 0;
    return XM_GUA_IDX(xm_yijing[idx].mark);
}

const char *xm_yao_label(uint8_t mark, uint8_t pos) {
    static char s_labels[6][2][8];
    static const char *s_mid[6] = {"", "二", "三", "四", "五", ""};
    if (pos < 1 || pos > 6) return "爻";
    const uint8_t yang = XM_GUA_LINE(mark, pos);
    char *buf = s_labels[pos - 1][yang];
    const char *num = yang ? "九" : "六";
    if (pos == 1) {
        (void)snprintf(buf, 8, "初%s", num);
    } else if (pos == 6) {
        (void)snprintf(buf, 8, "上%s", num);
    } else {
        (void)snprintf(buf, 8, "%s%s", num, s_mid[pos - 1]);
    }
    return buf;
}
