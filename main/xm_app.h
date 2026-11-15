#pragma once
/* 应用单例：聚合导航、时间、NVS、六爻与黄历表；L4 编排层。 */

#include "xm_huangli.h"
#include "xm_liuyao.h"
#include "xm_nav.h"
#include "xm_store.h"
#include "xm_time.h"
#include "xm_vm.h"

typedef struct xm_app_s {
    xm_nav_t nav;
    xm_time_t time;
    xm_store_t store;
    xm_liuyao_t liuyao;
    xm_hl_table_t hl_table;
    uint8_t err_flags;
    int battery_soc;
    bool ui_dirty;
    uint8_t toss[6];
} xm_app_t;

xm_status_t xm_app_init(xm_app_t *app, bool nvs_ready);
void xm_app_on_key(xm_app_t *app, xm_input_t input, int64_t now_us);
void xm_app_tick(xm_app_t *app, int64_t now_us);
xm_status_t xm_app_render(xm_app_t *app, xm_view_t *out, int64_t now_us, int soc);
bool xm_app_need_render(const xm_app_t *app);
void xm_app_mark_dirty(xm_app_t *app);
