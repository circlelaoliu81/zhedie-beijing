#include "xm_app.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "xm_err.h"
#include "xm_huangli_data.h"
#include "xm_pages.h"
#include "xm_yijing.h"

static const char *TAG = "xm_app";
static const char *NVS_NAMESPACE = "xm";

static uint8_t xm_app_random_toss(void) {
    uint8_t sum = 0;
    for (uint8_t coin = 0; coin < 3; ++coin) {
        sum += (uint8_t)((esp_random() & 1u) ? 3u : 2u);
    }
    return sum;
}

static void xm_app_open_fmt(uint8_t out[8]) {
    (void)xm_store_encode_fmt(out);
}

static esp_err_t xm_app_nvs_write(nvs_handle_t handle, const char *key,
                                  const void *value, size_t len) {
    esp_err_t err = nvs_set_blob(handle, key, value, len);
    if (err != ESP_OK) return err;
    return nvs_commit(handle);
}

static esp_err_t xm_app_save_ack(xm_app_t *app) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint8_t fmt[8] = {0};
    uint8_t ack = app->store.ack;
    xm_app_open_fmt(fmt);
    err = xm_app_nvs_write(handle, "fmt", fmt, sizeof(fmt));
    if (err == ESP_OK) err = xm_app_nvs_write(handle, "ack", &ack, sizeof(ack));
    nvs_close(handle);
    return err;
}

static esp_err_t xm_app_save_time(xm_app_t *app) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint8_t fmt[8] = {0};
    uint8_t time_blob[12] = {0};
    xm_app_open_fmt(fmt);
    (void)xm_store_encode_time(&app->store.time, time_blob);
    err = xm_app_nvs_write(handle, "fmt", fmt, sizeof(fmt));
    if (err == ESP_OK) err = xm_app_nvs_write(handle, "time", time_blob, sizeof(time_blob));
    nvs_close(handle);
    return err;
}

static esp_err_t xm_app_save_hist(xm_app_t *app) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint8_t fmt[8] = {0};
    uint8_t hist_blob[108] = {0};
    xm_app_open_fmt(fmt);
    (void)xm_store_save_hist(&app->store, hist_blob);
    err = xm_app_nvs_write(handle, "fmt", fmt, sizeof(fmt));
    if (err == ESP_OK) err = xm_app_nvs_write(handle, "liuyao", hist_blob, sizeof(hist_blob));
    nvs_close(handle);
    return err;
}

static size_t xm_app_read_blob(nvs_handle_t handle, const char *key,
                               uint8_t *buf, size_t cap) {
    size_t len = cap;
    if (nvs_get_blob(handle, key, buf, &len) != ESP_OK) return 0;
    return len;
}

static void xm_app_nvs_load(xm_app_t *app) {
    nvs_handle_t handle = 0;
    const esp_err_t open_err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (open_err == ESP_ERR_NVS_NOT_FOUND) {
        /* 全新设备连玄门 namespace 都还没有 —— 「没有存档」不是「存储损坏」。
         * 用 xm_store_init 的默认状态正常启动，否则每台新机首次开机都会挂 E3
         * 「存储降级：记录不可用」并被强制钉回免责页。 */
        return;
    }
    if (open_err != ESP_OK) {
        app->store.degraded = true;
        return;
    }
    uint8_t fmt[8] = {0};
    uint8_t ack[1] = {0};
    uint8_t time_blob[12] = {0};
    uint8_t hist_blob[108] = {0};
    uint8_t settings[8] = {0};
    const size_t fmt_len = xm_app_read_blob(handle, "fmt", fmt, sizeof(fmt));
    const size_t ack_len = xm_app_read_blob(handle, "ack", ack, sizeof(ack));
    const size_t time_len = xm_app_read_blob(handle, "time", time_blob, sizeof(time_blob));
    const size_t hist_len = xm_app_read_blob(handle, "liuyao", hist_blob, sizeof(hist_blob));
    const size_t settings_len = xm_app_read_blob(handle, "settings", settings, sizeof(settings));
    nvs_close(handle);

    /* 缺键补齐逻辑集中在 xm_store_load_boot（纯逻辑、可被 host 测试覆盖）：
     * save_ack / save_time / save_hist 各只写自己那部分，settings 从未写回，
     * 若缺键直传 xm_store_load，用户确认免责后的下一次开机必然永久判降级。 */
    const xm_status_t status = xm_store_load_boot(&app->store,
                                                  fmt, fmt_len, ack, ack_len,
                                                  time_blob, time_len,
                                                  hist_blob, hist_len,
                                                  settings, settings_len);
    if (status != XM_OK) {
        app->store.degraded = true;
    }
}

xm_status_t xm_app_init(xm_app_t *app, bool nvs_ready) {
    if (!app) return XM_ERR_INVALID_ARG;
    memset(app, 0, sizeof(*app));
    xm_nav_init(&app->nav);
    xm_time_init(&app->time);
    xm_store_init(&app->store);
    (void)xm_hl_init(&app->hl_table, xm_huangli_blob, xm_huangli_blob_size);
    (void)xm_yijing_init();
    app->battery_soc = -1;
    app->err_flags = XM_ERR_TIME_UNCALIBRATED_BIT;
    if (!nvs_ready) {
        app->store.degraded = true;
    } else {
        xm_app_nvs_load(app);
    }
    if (app->store.degraded) {
        app->err_flags |= XM_ERR_NVS_DEGRADED_BIT;
        app->nav.disclaimer_acked = false;
        app->nav.time_confirmed = false;
        app->nav.page = XM_PAGE_DISCLAIMER;
    } else {
        app->nav.disclaimer_acked = (app->store.ack == 1u);
        app->nav.page = app->nav.disclaimer_acked ? XM_PAGE_TIME : XM_PAGE_DISCLAIMER;
        app->nav.draft_date = app->store.time;
        app->time.base = app->store.time;
        app->time.confirmed = false;
    }
    xm_nav_set_error(&app->nav, app->err_flags);
    app->ui_dirty = true;
    return XM_OK;
}

void xm_app_on_key(xm_app_t *app, xm_input_t input, int64_t now_us) {
    if (!app) return;
    xm_nav_result_t result;
    if (xm_nav_dispatch(&app->nav, input, now_us, &result) != XM_OK) return;
    if (result.persist_ack) {
        app->store.ack = 1;
        if (xm_app_save_ack(app) != ESP_OK) ESP_LOGW(TAG, "免责确认写入失败");
    }
    if (result.persist_time) {
        xm_time_confirm(&app->time, &app->nav.draft_date, now_us);
        app->store.time = app->nav.draft_date;
        if (xm_app_save_time(app) != ESP_OK) ESP_LOGW(TAG, "设备时间写入失败");
    }
    if (result.cast_step && app->nav.cast_count > 0 && app->nav.cast_count <= 6) {
        const uint8_t idx = (uint8_t)(app->nav.cast_count - 1u);
        app->toss[idx] = xm_app_random_toss();
    }
    if (result.finish_cast) {
        app->liuyao = xm_liuyao_from(app->toss);
        const xm_datetime_t now = xm_time_now(&app->time, now_us);
        xm_hist_rec_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.dev_time = xm_hist_pack_time(&now.date);
        rec.toss_pack = xm_hist_pack_toss(app->toss);
        rec.ben_idx = app->liuyao.ben_idx;
        rec.changed_idx = app->liuyao.changed_idx;
        rec.moving_mask = app->liuyao.moving_mask;
        xm_hist_push(&app->store.hist, &rec);
        if (xm_app_save_hist(app) != ESP_OK) ESP_LOGW(TAG, "六爻历史写入失败");
    }
    if (result.clear_hist) {
        (void)xm_store_clear_hist(&app->store);
        if (xm_app_save_hist(app) != ESP_OK) ESP_LOGW(TAG, "清空历史写入失败");
    }
    if (result.action == XM_NAV_ACTION_RENDER) app->ui_dirty = true;
}

void xm_app_tick(xm_app_t *app, int64_t now_us) {
    if (!app) return;
    bool render = false;
    (void)xm_nav_tick(&app->nav, now_us, &render);
    const xm_datetime_t now = xm_time_now(&app->time, now_us);
    /* 只有「本次开机尚未授时」才挂 E1 红条；用户设完时间（xm_time_confirm → confirmed）
     * 必须能把它清掉，否则「时间未设定」会永久压在页面标题上（banner 固定画在 y=24..44）。 */
    uint8_t flags = xm_err_flags_compute(app->time.confirmed,
                                        xm_time_in_range(&now.date),
                                        app->store.degraded,
                                        app->hl_table.blob != NULL);
    if (app->battery_soc < 0) flags |= XM_ERR_BATTERY_UNAVAILABLE_BIT;
    if (flags != app->err_flags) {
        app->err_flags = flags;
        xm_nav_set_error(&app->nav, flags);
        render = true;
    }
    if (render) app->ui_dirty = true;
}

xm_status_t xm_app_render(xm_app_t *app, xm_view_t *out, int64_t now_us, int soc) {
    if (!app || !out) return XM_ERR_INVALID_ARG;
    app->battery_soc = soc;
    *out = xm_pages_render(&app->nav, &app->time, &app->store, &app->liuyao,
                           app->toss, &app->hl_table, now_us, soc, app->err_flags);
    app->ui_dirty = false;
    return XM_OK;
}

bool xm_app_need_render(const xm_app_t *app) {
    return app ? app->ui_dirty : false;
}

void xm_app_mark_dirty(xm_app_t *app) {
    if (app) app->ui_dirty = true;
}
