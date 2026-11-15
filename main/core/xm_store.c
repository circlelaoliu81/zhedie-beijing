#include "xm_store.h"

#include <string.h>
#include "xm_time.h"

static void put_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_u16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t xm_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    if (!data && len != 0) return 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void xm_hist_reset(xm_nvs_hist_t *hist) {
    memset(hist, 0, sizeof(*hist));
    hist->magic[0] = XM_HIST_MAGIC_0;
    hist->magic[1] = XM_HIST_MAGIC_1;
    hist->ver = XM_FORMAT_VERSION;
    hist->count = 0;
    hist->head = 0;
    hist->crc32 = 0;
}

void xm_store_init(xm_store_t *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
    memcpy(store->fmt.magic, XM_MAGIC_NVS, 4);
    store->fmt.ver = XM_FORMAT_VERSION;
    store->ack = 0;
    store->time.year = 2026;
    store->time.month = 11;
    store->time.day = 13;
    store->time.hour = 10;
    store->time.minute = 30;
    xm_hist_reset(&store->hist);
    store->degraded = false;
}

xm_status_t xm_store_encode_fmt(uint8_t out[8]) {
    if (!out) return XM_ERR_INVALID_ARG;
    memset(out, 0, 8);
    memcpy(out, XM_MAGIC_NVS, 4);
    out[4] = XM_FORMAT_VERSION;
    return XM_OK;
}

xm_status_t xm_store_decode_fmt(const uint8_t *buf, size_t len, xm_nvs_fmt_t *out) {
    if (!buf || !out || len != 8) return XM_ERR_NVS_BAD_FORMAT;
    if (memcmp(buf, XM_MAGIC_NVS, 4) != 0 || buf[4] != XM_FORMAT_VERSION) {
        return XM_ERR_NVS_BAD_FORMAT;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->magic, buf, 4);
    out->ver = buf[4];
    memcpy(out->rsv, buf + 5, 3);
    return XM_OK;
}

xm_status_t xm_store_encode_time(const xm_date_t *date, uint8_t out[12]) {
    if (!date || !out || !xm_date_valid(date)) return XM_ERR_INVALID_ARG;
    memset(out, 0, 12);
    put_u16le(out, date->year);
    out[2] = date->month;
    out[3] = date->day;
    out[4] = date->hour;
    out[5] = date->minute;
    out[6] = 1u; /* flags: 用户确认过的基准时间 */
    put_u32le(out + 8, xm_crc32(out, 6));
    return XM_OK;
}

xm_status_t xm_store_decode_time(const uint8_t *buf, size_t len, xm_date_t *out) {
    if (!buf || !out || len != 12) return XM_ERR_NVS_BAD_FORMAT;
    if (get_u32le(buf + 8) != xm_crc32(buf, 6)) return XM_ERR_NVS_BAD_FORMAT;
    xm_date_t date;
    memset(&date, 0, sizeof(date));
    date.year = get_u16le(buf);
    date.month = buf[2];
    date.day = buf[3];
    date.hour = buf[4];
    date.minute = buf[5];
    if (!xm_date_valid(&date)) return XM_ERR_NVS_BAD_FORMAT;
    *out = date;
    return XM_OK;
}

static void xm_hist_encode_rec(const xm_hist_rec_t *rec, uint8_t out[12]) {
    memset(out, 0, 12);
    put_u32le(out, rec->dev_time);
    put_u16le(out + 4, rec->toss_pack);
    out[6] = rec->ben_idx;
    out[7] = rec->changed_idx;
    out[8] = rec->moving_mask;
    memcpy(out + 9, rec->rsv, 3);
}

xm_status_t xm_store_encode_hist(const xm_nvs_hist_t *hist, uint8_t out[108]) {
    if (!hist || !out) return XM_ERR_INVALID_ARG;
    if (hist->count > 8 || hist->head > 7 || hist->magic[0] != XM_HIST_MAGIC_0 ||
        hist->magic[1] != XM_HIST_MAGIC_1 || hist->ver != XM_FORMAT_VERSION) {
        return XM_ERR_NVS_BAD_FORMAT;
    }
    memset(out, 0, 108);
    out[0] = hist->magic[0];
    out[1] = hist->magic[1];
    out[2] = hist->ver;
    out[3] = hist->count;
    out[4] = hist->head;
    memcpy(out + 5, hist->rsv, 4);
    for (uint8_t i = 0; i < 8; ++i) {
        xm_hist_encode_rec(&hist->rec[i], out + 9 + (size_t)i * 12);
    }
    put_u32le(out + 104, xm_crc32(out, 104));
    return XM_OK;
}

xm_status_t xm_store_decode_hist(const uint8_t *buf, size_t len, xm_nvs_hist_t *out) {
    if (!buf || !out || len != 108) return XM_ERR_NVS_BAD_FORMAT;
    if (buf[0] != XM_HIST_MAGIC_0 || buf[1] != XM_HIST_MAGIC_1 ||
        buf[2] != XM_FORMAT_VERSION || buf[3] > 8 || buf[4] > 7) {
        return XM_ERR_NVS_BAD_FORMAT;
    }
    if (get_u32le(buf + 104) != xm_crc32(buf, 104)) return XM_ERR_NVS_BAD_FORMAT;
    xm_nvs_hist_t hist;
    memset(&hist, 0, sizeof(hist));
    hist.magic[0] = buf[0];
    hist.magic[1] = buf[1];
    hist.ver = buf[2];
    hist.count = buf[3];
    hist.head = buf[4];
    memcpy(hist.rsv, buf + 5, 4);
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t *p = buf + 9 + (size_t)i * 12;
        hist.rec[i].dev_time = get_u32le(p);
        hist.rec[i].toss_pack = get_u16le(p + 4);
        hist.rec[i].ben_idx = p[6];
        hist.rec[i].changed_idx = p[7];
        hist.rec[i].moving_mask = p[8];
        memcpy(hist.rec[i].rsv, p + 9, 3);
        uint8_t toss[6] = {0};
        if (hist.rec[i].ben_idx > 63 || hist.rec[i].changed_idx > 63 ||
            hist.rec[i].moving_mask > 63 ||
            xm_hist_unpack_toss(hist.rec[i].toss_pack, toss) != XM_OK) {
            return XM_ERR_NVS_BAD_FORMAT;
        }
    }
    hist.crc32 = get_u32le(buf + 104);
    *out = hist;
    return XM_OK;
}

xm_status_t xm_store_load(xm_store_t *store,
                          const uint8_t *fmt, size_t fmt_len,
                          const uint8_t *ack, size_t ack_len,
                          const uint8_t *time_blob, size_t time_len,
                          const uint8_t *hist_blob, size_t hist_len,
                          const uint8_t *settings, size_t settings_len) {
    if (!store) return XM_ERR_INVALID_ARG;
    xm_nvs_fmt_t decoded_fmt;
    xm_date_t decoded_time;
    xm_nvs_hist_t decoded_hist;
    xm_status_t status = xm_store_decode_fmt(fmt, fmt_len, &decoded_fmt);
    if (status != XM_OK) {
        store->degraded = true;
        return status;
    }
    if (!ack || ack_len != 1 || (ack[0] != 0 && ack[0] != 1)) {
        store->degraded = true;
        return XM_ERR_NVS_BAD_FORMAT;
    }
    status = xm_store_decode_time(time_blob, time_len, &decoded_time);
    if (status != XM_OK) {
        store->degraded = true;
        return status;
    }
    status = xm_store_decode_hist(hist_blob, hist_len, &decoded_hist);
    if (status != XM_OK) {
        store->degraded = true;
        return status;
    }
    if (!settings || settings_len != 8) {
        store->degraded = true;
        return XM_ERR_NVS_BAD_FORMAT;
    }
    store->fmt = decoded_fmt;
    store->ack = ack[0];
    store->time = decoded_time;
    store->hist = decoded_hist;
    memcpy(store->settings, settings, 8);
    store->degraded = false;
    return XM_OK;
}

xm_status_t xm_store_load_boot(xm_store_t *store,
                               const uint8_t *fmt, size_t fmt_len,
                               const uint8_t *ack, size_t ack_len,
                               const uint8_t *time_blob, size_t time_len,
                               const uint8_t *hist_blob, size_t hist_len,
                               const uint8_t *settings, size_t settings_len) {
    if (!store) return XM_ERR_INVALID_ARG;
    /* 开机加载：键不存在（len==0）时用 store 当前默认状态编码的合法 blob
     * 补齐。xm_store_load 要求五键齐全，而 save_ack / save_time / save_hist
     * 各自只写自己那部分、settings 从未写回，缺键直传会让用户确认免责后的
     * 下一次开机永久判「存储降级」。读到非空值一律原样交给 load 校验，
     * 真正的坏数据仍会判降级，不会被补齐掩盖。 */
    uint8_t def_fmt[8] = {0};
    uint8_t def_ack[1] = {0};
    uint8_t def_time[12] = {0};
    uint8_t def_hist[108] = {0};
    uint8_t def_settings[8] = {0};
    if (xm_store_encode_fmt(def_fmt) != XM_OK ||
        xm_store_encode_time(&store->time, def_time) != XM_OK ||
        xm_store_save_hist(store, def_hist) != XM_OK) {
        store->degraded = true;
        return XM_ERR_NVS_BAD_FORMAT;
    }
    memcpy(def_settings, store->settings, sizeof(def_settings));
    return xm_store_load(store,
                         fmt_len ? fmt : def_fmt,
                         fmt_len ? fmt_len : sizeof(def_fmt),
                         ack_len ? ack : def_ack,
                         ack_len ? ack_len : sizeof(def_ack),
                         time_len ? time_blob : def_time,
                         time_len ? time_len : sizeof(def_time),
                         hist_len ? hist_blob : def_hist,
                         hist_len ? hist_len : sizeof(def_hist),
                         settings_len ? settings : def_settings,
                         settings_len ? settings_len : sizeof(def_settings));
}

xm_status_t xm_store_save_hist(const xm_store_t *store, uint8_t out[108]) {
    if (!store) return XM_ERR_INVALID_ARG;
    return xm_store_encode_hist(&store->hist, out);
}

xm_status_t xm_store_clear_hist(xm_store_t *store) {
    if (!store) return XM_ERR_INVALID_ARG;
    xm_hist_reset(&store->hist);
    return XM_OK;
}

void xm_hist_push(xm_nvs_hist_t *hist, const xm_hist_rec_t *rec) {
    if (!hist || !rec) return;
    hist->rec[hist->head] = *rec;
    hist->head = (uint8_t)((hist->head + 1) % 8);
    if (hist->count < 8) hist->count++;
    hist->crc32 = 0;
}

xm_hist_rec_t xm_hist_at(const xm_nvs_hist_t *hist, uint8_t newest_first_index) {
    xm_hist_rec_t empty;
    memset(&empty, 0, sizeof(empty));
    if (!hist || hist->count == 0 || newest_first_index >= hist->count) return empty;
    const uint8_t physical = (uint8_t)((hist->head + 16 - 1 - newest_first_index) % 8);
    return hist->rec[physical];
}

uint32_t xm_hist_pack_time(const xm_date_t *date) {
    if (!date) return 0;
    uint32_t y = date->year >= XM_DATA_START_Y ? (uint32_t)(date->year - XM_DATA_START_Y) : 0u;
    if (y > 63u) y = 63u;
    return (y << 26) | ((uint32_t)date->month << 22) |
           ((uint32_t)date->day << 17) | ((uint32_t)date->hour << 12) |
           ((uint32_t)date->minute << 6);
}

xm_date_t xm_hist_unpack_time(uint32_t packed) {
    xm_date_t date;
    memset(&date, 0, sizeof(date));
    date.year = (uint16_t)(XM_DATA_START_Y + ((packed >> 26) & 0x3Fu));
    date.month = (uint8_t)((packed >> 22) & 0x0Fu);
    date.day = (uint8_t)((packed >> 17) & 0x1Fu);
    date.hour = (uint8_t)((packed >> 12) & 0x1Fu);
    date.minute = (uint8_t)((packed >> 6) & 0x3Fu);
    return date;
}

uint16_t xm_hist_pack_toss(const uint8_t toss[6]) {
    uint16_t packed = 0;
    if (!toss) return 0;
    for (uint8_t i = 0; i < 6; ++i) {
        const uint16_t code = (uint16_t)((toss[i] >= 6 && toss[i] <= 9) ? toss[i] - 6 : 0);
        packed |= (uint16_t)(code << (i * 2));
    }
    return packed;
}

xm_status_t xm_hist_unpack_toss(uint16_t packed, uint8_t out[6]) {
    if (!out) return XM_ERR_INVALID_ARG;
    for (uint8_t i = 0; i < 6; ++i) {
        const uint16_t code = (packed >> (i * 2)) & 0x3u;
        if (code > 3u) return XM_ERR_NVS_BAD_FORMAT;
        out[i] = (uint8_t)(6u + code);
    }
    return XM_OK;
}
