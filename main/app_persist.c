#include "app_persist.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "fb_save.h"

static const char *TAG = "zdbz.save";

#define SAVE_NAMESPACE "zhedie"
#define SAVE_KEY       "slot0"

/* NVS 打开失败（比如分区被写坏）不应该让游戏玩不下去：
 * 所有接口降级成「无存档」，只打一条日志。 */
static bool store_open(nvs_handle_t *out, nvs_open_mode_t mode) {
    esp_err_t err = nvs_open(SAVE_NAMESPACE, mode, out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 打开失败：%s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool app_persist_resume(fb_app_t *app) {
    if (!app) return false;

    nvs_handle_t h;
    if (!store_open(&h, NVS_READONLY)) return false;

    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, SAVE_KEY, NULL, &len);
    if (err != ESP_OK || len == 0 || len > FB_SAVE_MAX) {
        nvs_close(h);
        return false;
    }

    uint8_t buf[FB_SAVE_MAX];
    size_t got = len;
    err = nvs_get_blob(h, SAVE_KEY, buf, &got);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "读存档失败：%s", esp_err_to_name(err));
        return false;
    }
    if (!fb_save_unpack(app, buf, got)) {
        /* 坏档：删掉，别让它每天开机都试一次 */
        ESP_LOGW(TAG, "存档校验失败（%u 字节），已丢弃", (unsigned)got);
        app_persist_clear();
        return false;
    }
    ESP_LOGI(TAG, "读档成功：第 %d 天 现金 %d 健康 %d", app->st.day,
             (int)app->st.cash, app->st.health);
    return true;
}

void app_persist_clear(void) {
    nvs_handle_t h;
    if (!store_open(&h, NVS_READWRITE)) return;
    (void)nvs_erase_key(h, SAVE_KEY);
    (void)nvs_commit(h);
    nvs_close(h);
}

void app_persist_tick(fb_app_t *app) {
    if (!app) return;

    if (app->save_clear) {
        app->save_clear = false;
        app->save_dirty = false;
        app_persist_clear();
        ESP_LOGI(TAG, "本局结束，存档已清除");
        return;
    }
    if (!app->save_dirty) return;
    app->save_dirty = false;

    /* 兜底：状态不对（已结束）就不写，顺手清掉旧档 */
    if (app->st.ending != FB_END_NONE || app->scr == FB_SCR_ENDING) {
        app_persist_clear();
        ESP_LOGI(TAG, "本局结束，存档已清除");
        return;
    }

    uint8_t buf[FB_SAVE_MAX];
    size_t n = fb_save_pack(app, buf, sizeof(buf));
    if (n == 0) return;

    nvs_handle_t h;
    if (!store_open(&h, NVS_READWRITE)) return;
    esp_err_t err = nvs_set_blob(h, SAVE_KEY, buf, n);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "写存档失败：%s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "存档已写：第 %d 天（%u 字节）", app->st.day, (unsigned)n);
}
