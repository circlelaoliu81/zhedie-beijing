#include "xm_battery.h"

#include <stdio.h>
#include "bsp_battery.h"

static lv_obj_t *s_battery_label = NULL;
static lv_timer_t *s_battery_timer = NULL;
static char s_battery_text[16] = "";

static void xm_battery_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_battery_label) return;
    const int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_add_flag(s_battery_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    (void)snprintf(s_battery_text, sizeof(s_battery_text), "%d%%", soc);
    lv_label_set_text(s_battery_label, s_battery_text);
    lv_obj_remove_flag(s_battery_label, LV_OBJ_FLAG_HIDDEN);
}

void xm_battery_attach(lv_obj_t *label) {
    s_battery_label = label;
    s_battery_text[0] = '\0';
}

void xm_battery_start(void) {
    if (!s_battery_label) return;
    if (!s_battery_timer) {
        s_battery_timer = lv_timer_create(xm_battery_timer_cb, 5000, NULL);
    }
    xm_battery_timer_cb(s_battery_timer);
}

void xm_battery_stop(void) {
    if (s_battery_timer) {
        lv_timer_delete(s_battery_timer);
        s_battery_timer = NULL;
    }
    s_battery_label = NULL;
    s_battery_text[0] = '\0';
}
