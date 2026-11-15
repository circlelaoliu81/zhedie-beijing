// main/main.c —— 折叠北京应用入口：ESP-IDF / BSP / LVGL 生命周期编排。
//
// 启动顺序：NVS → 应用状态 → I2C / Display / LVGL → 中文字体 fallback
// → 首屏渲染 → 三键输入任务。
// 界面全部由 fb_app 的页面状态机生成，不复用基线 demo 菜单与 ui_pixel 外壳。
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include "fb_app.h"
#include "fb_art.h"
#include "xm_build_id.h"
#include "xm_fonts.h"
#include "xm_input_task.h"
#include "xm_ui.h"

static const char *TAG = "zhedie";

/* 应用单例：生命周期与 app_main 相同；输入任务只通过 fb_app_* 访问。 */
static fb_app_t s_app;

/* 视图模型放静态区而不是 app_main 的栈上：xm_view_t 含 24 行 + 32 框 +
 * 12 个图标，约 1.8KB。主任务栈默认只有 3584 字节，按值放栈上会踩线。
 * 首屏只在 app_main 里组装一次，之后由输入任务用自己栈上的副本。 */
static xm_view_t s_view;

static void fb_log_build_identity(void) {
    ESP_LOGI(TAG,
             "折叠北京 %s build=%s idf=%s lvgl=%s fontconv=%s fontcomp=%d charset=%s",
             XM_APP_VERSION, XM_BUILD_ID, XM_IDF_VERSION, XM_LVGL_VERSION,
             XM_FONTCONV_VERSION, (int)LV_USE_FONT_COMPRESSED, XM_CHARSET_SHA256);
}

void app_main(void) {
    /* 先验美术：嵌入的二进制与生成清单长度不符时，设备上的表现是花屏或黑块
     * 而不是报错，所以要在点亮背光之前主动检查一次。 */
    uint32_t art_bad = 0;
    if (!fb_art_selftest(&art_bad)) {
        ESP_LOGE(TAG, "美术资源自检失败：%u 项长度与清单不符",
                 (unsigned)art_bad);
    }

    fb_log_build_identity();

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 初始化失败：%s（本局不落盘，功能不受影响）",
                 esp_err_to_name(nvs_err));
    }

    /* 用开机时刻做随机种子，保证每局事件序列不同 */
    fb_app_init(&s_app, (uint32_t)(esp_timer_get_time() & 0x7fffffff));

    (void)bsp_i2c_init();
    (void)bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示或 LVGL 初始化失败，折叠北京无法继续");
        return;
    }
    bsp_display_backlight(100);
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用，状态条右上电量位隐藏");
    }

    const int soc = bsp_battery_soc();
    if (bsp_lvgl_lock(1000)) {
        xm_fonts_init();
        fb_app_render(&s_app, &s_view);
        if (s_view.line_n > 0u) {
            ESP_LOGI(TAG,
                     "首屏视图: line_n=%u box_n=%u icon_n=%u image=%u",
                     (unsigned)s_view.line_n, (unsigned)s_view.box_n,
                     (unsigned)s_view.icon_n, (unsigned)s_view.image_id);
            xm_ui_apply_locked(&s_view, soc);
        } else {
            ESP_LOGE(TAG, "首屏视图为空，检查 fb_app_render");
        }
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "首屏 LVGL 锁获取失败");
    }

    esp_err_t input_err = xm_input_task_start(&s_app);
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "输入任务启动失败：%s", esp_err_to_name(input_err));
    }

    ESP_LOGI(TAG, "折叠北京就绪：第 %d 天 现金 %d 健康 %d 电量 %d",
             s_app.st.day, (int)s_app.st.cash, s_app.st.health, soc);
}
