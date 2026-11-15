#include "xm_huangli.h"

#include <string.h>
#include "xm_huangli_data.h"
#include "xm_store.h"
#include "xm_time.h"

static uint16_t get_u16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

xm_status_t xm_hl_init(xm_hl_table_t *table, const uint8_t *blob, uint32_t size) {
    if (!table || !blob || size < 32) return XM_ERR_HL_BAD_BLOB;
    memset(table, 0, sizeof(*table));
    if (memcmp(blob, XM_MAGIC_HL, 4) != 0) return XM_ERR_HL_BAD_BLOB;
    xm_hl_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = get_u16le(blob + 4);
    h.day_count = get_u16le(blob + 6);
    h.base_days = get_u32le(blob + 8);
    h.str_pool_off = get_u32le(blob + 12);
    h.str_pool_bytes = get_u32le(blob + 16);
    h.rec_off = get_u32le(blob + 20);
    h.rec_size = get_u16le(blob + 24);
    h.list_pool_off = get_u16le(blob + 26);
    h.crc32 = get_u32le(blob + 28);
    if (h.version != XM_FORMAT_VERSION || h.day_count != XM_HL_DAY_COUNT ||
        h.base_days != XM_HL_BASE_DAYS || h.str_pool_off != 32 ||
        h.rec_size != XM_HL_RECORD_SIZE) {
        return XM_ERR_HL_BAD_BLOB;
    }
    if (h.rec_off + (uint32_t)h.day_count * h.rec_size > size) return XM_ERR_HL_BAD_BLOB;
    if (h.list_pool_off <= h.str_pool_off || h.list_pool_off >= h.rec_off ||
        h.str_pool_off + h.str_pool_bytes > size) {
        return XM_ERR_HL_BAD_BLOB;
    }
    if (xm_crc32(blob + 32, size - 32) != h.crc32) return XM_ERR_HL_BAD_BLOB;
    table->blob = blob;
    table->size = size;
    table->hdr = h;
    return XM_OK;
}

const char *xm_hl_str(const xm_hl_table_t *table, uint16_t index) {
    if (!table || !table->blob) return NULL;
    const uint8_t *pool = table->blob + table->hdr.str_pool_off;
    if (table->hdr.str_pool_bytes < 2) return NULL;
    const uint16_t count = get_u16le(pool);
    if (index >= count) return NULL;
    const uint32_t table_bytes = 2u + 2u * (uint32_t)count;
    if (table_bytes > table->hdr.str_pool_bytes) return NULL;
    const uint16_t off = get_u16le(pool + 2 + 2u * (uint32_t)index);
    if (off >= table->hdr.str_pool_bytes) return NULL;
    const char *str = (const char *)(pool + off);
    const uint32_t remain = table->hdr.str_pool_bytes - off;
    if (remain == 0 || memchr(str, 0, remain) == NULL) return NULL;
    if (str[0] == 0) return NULL;
    return str;
}

static const char *xm_hl_list_item(const xm_hl_table_t *table, uint16_t list_off,
                                  uint32_t *cursor) {
    const uint32_t list_pool_off = table->hdr.list_pool_off;
    const uint32_t list_pool_bytes = table->hdr.rec_off - list_pool_off;
    const uint8_t *list_pool = table->blob + list_pool_off;
    if (list_pool_bytes < 2) return NULL;
    const uint16_t term_count = get_u16le(list_pool);
    const uint32_t map_bytes = 2u + 2u * (uint32_t)term_count;
    if (map_bytes > list_pool_bytes) return NULL;
    const uint32_t pos = list_off + *cursor;
    if (pos >= list_pool_bytes) return NULL;
    const uint8_t code = list_pool[pos];
    uint16_t str_idx = 0;
    if (code != 0xFFu) {
        if (code >= term_count) return NULL;
        str_idx = get_u16le(list_pool + 2 + 2u * (uint32_t)code);
        *cursor += 1;
    } else {
        if (pos + 3 > list_pool_bytes) return NULL;
        str_idx = get_u16le(list_pool + pos + 1);
        *cursor += 3;
    }
    return xm_hl_str(table, str_idx);
}

static xm_status_t xm_hl_fill_field(const xm_hl_table_t *table, uint16_t off,
                                    uint8_t count, xm_field_t *out) {
    if (!out || count > 32) return XM_ERR_HL_FIELD_MISSING;
    memset(out, 0, sizeof(*out));
    uint32_t cursor = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const char *item = xm_hl_list_item(table, off, &cursor);
        if (!item) return XM_ERR_HL_FIELD_MISSING;
        out->items[i] = item;
    }
    out->count = count;
    return XM_OK;
}

/* record 布局（64B，生成器 tools/xm/gen_huangli.py 必须与此逐字节一致）：
 *   [0,2)lunar_y [2,4)lunar_m [4,6)lunar_d [6]leap [7,9)gz_y [9,11)gz_m [11,13)gz_d
 *   [13,15)yi_off [15]yi_cnt [16,18)ji_off [18]ji_cnt [19,21)jishen_off [21]jishen_cnt
 *   [22,24)xiongsha_off [24]xiongsha_cnt [25,27)tianshen [27]zhixing [28]xiu_raw
 *   [29,31)chong [31]sha [32,34)pengzu_gan [34,36)pengzu_zhi
 *   [36]pos_xi [37]pos_fu [38]pos_cai [39]nayin [40,52)hour_gz[12] [52,54)hour_luck [54]seed
 *   警告：pengzu_zhi 为 2 字节，pos_xi 必须从 36 起，切勿再压到 35。 */
xm_status_t xm_hl_lookup(const xm_hl_table_t *table, const xm_date_t *date, xm_hl_day_t *out) {
    if (!table || !table->blob || !out) return XM_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!xm_date_valid(date)) return XM_ERR_BAD_DATE;
    const int64_t days = xm_days_from_civil(date->year, date->month, date->day);
    const int64_t index = days - (int64_t)table->hdr.base_days;
    if (index < 0 || index >= (int64_t)table->hdr.day_count) {
        return XM_ERR_HL_OUT_OF_RANGE;
    }
    const uint8_t *rec = table->blob + table->hdr.rec_off + (uint32_t)index * 64u;
    out->lunar_y = xm_hl_str(table, get_u16le(rec + 0));
    out->lunar_m = xm_hl_str(table, get_u16le(rec + 2));
    out->lunar_d = xm_hl_str(table, get_u16le(rec + 4));
    out->lunar_leap = (rec[6] & 1u) != 0;
    out->gz_y = xm_hl_str(table, get_u16le(rec + 7));
    out->gz_m = xm_hl_str(table, get_u16le(rec + 9));
    out->gz_d = xm_hl_str(table, get_u16le(rec + 11));
    out->tianshen = xm_hl_str(table, get_u16le(rec + 25));
    out->chong = xm_hl_str(table, get_u16le(rec + 29));
    out->pengzu_gan = xm_hl_str(table, get_u16le(rec + 32));
    out->pengzu_zhi = xm_hl_str(table, get_u16le(rec + 34));
    if (!out->lunar_y || !out->lunar_m || !out->lunar_d || !out->gz_y ||
        !out->gz_m || !out->gz_d || !out->tianshen || !out->chong ||
        !out->pengzu_gan || !out->pengzu_zhi) {
        return XM_ERR_HL_FIELD_MISSING;
    }
    const uint8_t zhixing = rec[27];
    const uint8_t xiu_raw = rec[28];
    const uint8_t sha = rec[31];
    const uint8_t nayin = rec[39];
    if (zhixing >= 12 || (xiu_raw & 0x1Fu) >= 28 || sha >= 8 || nayin >= 30) {
        return XM_ERR_HL_FIELD_MISSING;
    }
    out->zhixing = xm_zhixing_table[zhixing];
    out->xiu = xm_xiu_table[xiu_raw & 0x1Fu];
    out->xiu_luck = (xiu_raw & 0x80u) != 0;
    out->sha = xm_dir_table[sha];
    if (rec[36] >= 8 || rec[37] >= 8 || rec[38] >= 8) return XM_ERR_HL_FIELD_MISSING;
    out->pos_xi = xm_dir_table[rec[36]];
    out->pos_fu = xm_dir_table[rec[37]];
    out->pos_cai = xm_dir_table[rec[38]];
    out->nayin = xm_nayin_table[nayin];
    if (!out->zhixing || !out->xiu || !out->sha || !out->pos_xi ||
        !out->pos_fu || !out->pos_cai || !out->nayin) {
        return XM_ERR_HL_FIELD_MISSING;
    }
    if (xm_hl_fill_field(table, get_u16le(rec + 13), rec[15], &out->yi) != XM_OK ||
        xm_hl_fill_field(table, get_u16le(rec + 16), rec[18], &out->ji) != XM_OK ||
        xm_hl_fill_field(table, get_u16le(rec + 19), rec[21], &out->jishen) != XM_OK ||
        xm_hl_fill_field(table, get_u16le(rec + 22), rec[24], &out->xiongsha) != XM_OK) {
        return XM_ERR_HL_FIELD_MISSING;
    }
    for (uint8_t i = 0; i < 12; ++i) {
        const uint8_t gz = rec[40 + i];
        if (gz >= 60) return XM_ERR_HL_FIELD_MISSING;
        out->hour_gz[i] = xm_jiazi_table[gz];
        if (!out->hour_gz[i]) return XM_ERR_HL_FIELD_MISSING;
    }
    out->hour_luck = get_u16le(rec + 52);
    out->hour_idx_seed = rec[54];
    return XM_OK;
}
