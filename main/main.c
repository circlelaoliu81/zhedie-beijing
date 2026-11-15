// main/main.c —— 玄门应用入口：ESP-IDF / BSP / LVGL 生命周期编排。
//
// 启动顺序：NVS 初始化 → 应用状态装载 → I2C / Display / LVGL → 中文字体 fallback
// → 首屏视图模型 → 输入队列与三键回调。任何 NVS / 电量失败都走软降级，不阻塞 UI。
// 本文件彻底移除基线 DEMOS[] 菜单与 ui_pixel 外壳；demo 源文件保留在磁盘但不链接。
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "xm_app.h"
#include "xm_battery.h"
#include "xm_build_id.h"
#include "xm_fonts.h"
#include "xm_input_task.h"
#include "xm_ui.h"

static const char *TAG = "xuanmen";

/* 应用单例：生命周期与 app_main 相同；输入任务只通过 xm_app_* 访问。 */
static xm_app_t s_app;

/**
 * 打印本轮固件的可追溯信息。
 *
 * 用途：USB-Serial-JTAG 控制台与关于页、预览 manifest、归档 manifest 共用同一组版本值。
 * fontcomp 是运行时构建指纹：assets/fonts/xm_font_*.c 是压缩位图字体，
 * LV_USE_FONT_COMPRESSED 必须为 1，否则所有中文 label 取位图失败、整屏无字。
 * 副作用：仅写串口日志；不访问 NVS、不访问 LVGL、不阻塞。
 * 调用前提：ESP_LOG 已由 ESP-IDF 初始化，xm_build_id.h 已由构建期流水线生成。
 */
static void xm_log_build_identity(void) {
    ESP_LOGI(TAG,
             "玄门 %s build=%s data=%s idf=%s lvgl=%s fontconv=%s fontcomp=%d charset=%s data_sha=%s",
             XM_APP_VERSION, XM_BUILD_ID, XM_DATA_EPOCH, XM_IDF_VERSION,
             XM_LVGL_VERSION, XM_FONTCONV_VERSION, (int)LV_USE_FONT_COMPRESSED,
             XM_CHARSET_SHA256, XM_DATA_SHA256);
}

void app_main(void) {
    xm_log_build_identity();

    /* NVS 分区异常时不擦除、不迁移：只把 store 标为降级并重新走免责/时间确认。 */
    esp_err_t nvs_err = nvs_flash_init();
    const bool nvs_ready = (nvs_err == ESP_OK);
    if (!nvs_ready) {
        ESP_LOGE(TAG, "NVS 初始化失败：%s；历史与已存时间按降级处理", esp_err_to_name(nvs_err));
    }

    (void)xm_app_init(&s_app, nvs_ready);

    (void)bsp_i2c_init();
    (void)bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示或 LVGL 初始化失败，玄门无法继续");
        return;
    }
    bsp_display_backlight(100);
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，状态条右上电量位隐藏");
    }

    const int soc = bsp_battery_soc();
    if (bsp_lvgl_lock(1000)) {
        xm_fonts_init();
        xm_view_t view;
        const xm_status_t rst = xm_app_render(&s_app, &view, esp_timer_get_time(), soc);
        if (rst == XM_OK && view.line_n > 0u) {
            ESP_LOGI(TAG, "首屏视图: line_n=%u box_n=%u disp=%p scr_before=%p",
                     (unsigned)view.line_n, (unsigned)view.box_n,
                     (void *)lv_display_get_default(), (void *)lv_scr_act());
            xm_ui_apply_locked(&view, soc);
            ESP_LOGI(TAG, "首屏已提交: scr_after=%p", (void *)lv_scr_act());
        } else {
            ESP_LOGE(TAG, "首屏视图为空或渲染失败: status=%d line_n=%u",
                     (int)rst, (unsigned)view.line_n);
        }
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "首屏 LVGL 锁获取失败");
    }

    esp_err_t input_err = xm_input_task_start(&s_app);
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "输入任务启动失败：%s", esp_err_to_name(input_err));
    }

    ESP_LOGI(TAG, "玄门就绪：免责=%d 时间确认=%d NVS降级=%d 电量=%d",
             s_app.nav.disclaimer_acked, s_app.nav.time_confirmed,
             s_app.store.degraded, soc);
}
