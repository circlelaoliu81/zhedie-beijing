#pragma once
/* 三键导航状态机：纯逻辑、可 host test；不接触 LVGL / NVS / 随机源。 */

#include "xm_types.h"

typedef enum {
    XM_NAV_ACTION_NONE = 0,
    XM_NAV_ACTION_RENDER
} xm_nav_action_t;

typedef struct {
    xm_page_t page;
    xm_nav_action_t action;
    bool persist_ack;
    bool persist_time;
    bool cast_step;
    bool finish_cast;
    bool clear_hist;
} xm_nav_result_t;

void xm_nav_init(xm_nav_t *nav);
void xm_nav_push(xm_nav_t *nav, xm_page_t page);
xm_page_t xm_nav_pop(xm_nav_t *nav);
xm_status_t xm_nav_dispatch(xm_nav_t *nav, xm_input_t input, int64_t now_us,
                            xm_nav_result_t *out);
xm_status_t xm_nav_tick(xm_nav_t *nav, int64_t now_us, bool *render);
void xm_nav_set_error(xm_nav_t *nav, uint8_t flags);
uint8_t xm_nav_error_flags(const xm_nav_t *nav);
