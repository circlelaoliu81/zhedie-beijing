#pragma once
/* XMHL v1 二进制表解析：14 类字段、12 时辰、吉时位图，全部只读查表。 */

#include "xm_types.h"

#define XM_HL_HEADER_SIZE 32u
#define XM_HL_RECORD_SIZE 64u

typedef struct {
    uint16_t version;
    uint16_t day_count;
    uint32_t base_days;
    uint32_t str_pool_off;
    uint32_t str_pool_bytes;
    uint32_t rec_off;
    uint16_t rec_size;
    uint16_t list_pool_off;
    uint32_t crc32;
} xm_hl_header_t;

typedef struct {
    const uint8_t *blob;
    uint32_t size;
    xm_hl_header_t hdr;
} xm_hl_table_t;

typedef struct {
    const char *lunar_y;
    const char *lunar_m;
    const char *lunar_d;
    bool lunar_leap;
    const char *gz_y;
    const char *gz_m;
    const char *gz_d;
    xm_field_t yi;
    xm_field_t ji;
    xm_field_t jishen;
    xm_field_t xiongsha;
    const char *tianshen;
    const char *zhixing;
    const char *xiu;
    bool xiu_luck;
    const char *chong;
    const char *sha;
    const char *pengzu_gan;
    const char *pengzu_zhi;
    const char *pos_xi;
    const char *pos_fu;
    const char *pos_cai;
    const char *nayin;
    const char *hour_gz[12];
    uint16_t hour_luck;
    uint8_t hour_idx_seed;
} xm_hl_day_t;

xm_status_t xm_hl_init(xm_hl_table_t *table, const uint8_t *blob, uint32_t size);
xm_status_t xm_hl_lookup(const xm_hl_table_t *table, const xm_date_t *date, xm_hl_day_t *out);
const char *xm_hl_str(const xm_hl_table_t *table, uint16_t index);
