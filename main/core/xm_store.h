#pragma once
/* NVS 编解码纯逻辑：magic/version/CRC32/8 条环形历史，不调用 nvs_*。 */

#include "xm_types.h"

#define XM_NVS_FMT_SIZE 8u
#define XM_NVS_TIME_SIZE 12u
#define XM_NVS_HIST_SIZE 108u
#define XM_NVS_SETTINGS_SIZE 8u
#define XM_HIST_CAPACITY 8u

typedef struct {
    char magic[4];
    uint8_t ver;
    uint8_t rsv[3];
} xm_nvs_fmt_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t flags;
    uint32_t crc32;
} xm_nvs_time_t;

typedef struct {
    uint8_t magic[2];
    uint8_t ver;
    uint8_t count;
    uint8_t head;
    uint8_t rsv[4];
    xm_hist_rec_t rec[8];
    uint32_t crc32;
} xm_nvs_hist_t;

typedef struct {
    xm_nvs_fmt_t fmt;
    uint8_t ack;
    xm_date_t time;
    xm_nvs_hist_t hist;
    uint8_t settings[8];
    bool degraded;
} xm_store_t;

uint32_t xm_crc32(const uint8_t *data, size_t len);
void xm_store_init(xm_store_t *store);
xm_status_t xm_store_load(xm_store_t *store,
                          const uint8_t *fmt, size_t fmt_len,
                          const uint8_t *ack, size_t ack_len,
                          const uint8_t *time_blob, size_t time_len,
                          const uint8_t *hist_blob, size_t hist_len,
                          const uint8_t *settings, size_t settings_len);
/* 开机加载：允许部分键缺失（全新设备 / 只写过部分键），缺键用默认状态补齐。
 * 与 xm_store_load 的区别仅在此；读到非空值仍按原样严格校验。 */
xm_status_t xm_store_load_boot(xm_store_t *store,
                               const uint8_t *fmt, size_t fmt_len,
                               const uint8_t *ack, size_t ack_len,
                               const uint8_t *time_blob, size_t time_len,
                               const uint8_t *hist_blob, size_t hist_len,
                               const uint8_t *settings, size_t settings_len);
xm_status_t xm_store_encode_fmt(uint8_t out[8]);
xm_status_t xm_store_decode_fmt(const uint8_t *buf, size_t len, xm_nvs_fmt_t *out);
xm_status_t xm_store_encode_time(const xm_date_t *date, uint8_t out[12]);
xm_status_t xm_store_decode_time(const uint8_t *buf, size_t len, xm_date_t *out);
xm_status_t xm_store_encode_hist(const xm_nvs_hist_t *hist, uint8_t out[108]);
xm_status_t xm_store_decode_hist(const uint8_t *buf, size_t len, xm_nvs_hist_t *out);
xm_status_t xm_store_save_hist(const xm_store_t *store, uint8_t out[108]);
xm_status_t xm_store_clear_hist(xm_store_t *store);
void xm_hist_push(xm_nvs_hist_t *hist, const xm_hist_rec_t *rec);
xm_hist_rec_t xm_hist_at(const xm_nvs_hist_t *hist, uint8_t newest_first_index);
uint32_t xm_hist_pack_time(const xm_date_t *date);
xm_date_t xm_hist_unpack_time(uint32_t packed);
uint16_t xm_hist_pack_toss(const uint8_t toss[6]);
xm_status_t xm_hist_unpack_toss(uint16_t packed, uint8_t out[6]);
