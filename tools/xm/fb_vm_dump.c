/*
 * fb_vm_dump.c —— 折叠北京主机端视图转储器。
 *
 * 直接调用与固件完全相同的纯逻辑层（fb_app_init / fb_app_on_key / fb_app_render），
 * 把若干代表性页面的 xm_view_t 以 JSON 输出，供 fb_preview.py 渲染成 240x320 PNG。
 * 不依赖 ESP-IDF / LVGL / FreeRTOS。
 *
 * 编译：
 *   cc -std=c11 -Wall -Wextra -Imain -Imain/core \
 *      tools/xm/fb_vm_dump.c main/fb_app.c main/fb_game.c -o /tmp/fb_vm_dump
 */
#include <stdio.h>
#include <string.h>

#include "fb_app.h"
#include "xm_vm.h"

/* id → 图名。顺序必须与 fb_art.h 的 fb_icon_id_t 和 fb_scene_id_t 一致；
 * 预览器据此去 assets/art/ 取同名 PNG。 */
static const char *const k_scene_names[] = {
    "intro_room", "home_street", "market_stall", "delivery_road", "gig_warehouse",
    "ride_car", "live_room", "report_night", "event_rain", "ending_dawn",
};
static const char *const k_icon_names[] = {
    NULL, "cash", "heart", "bolt", "star", "cal", "work", "rest", "card", "box",
    "door", "info", "clock",
};

static void json_str(const char *s) {
    fputc('"', stdout);
    if (s) {
        for (const char *p = s; *p; ++p) {
            unsigned char ch = (unsigned char)*p;
            switch (ch) {
                case '"':  fputs("\\\"", stdout); break;
                case '\\': fputs("\\\\", stdout); break;
                case '\n': fputs("\\n", stdout); break;
                default:
                    if (ch < 0x20) fprintf(stdout, "\\u%04x", ch);
                    else fputc((int)ch, stdout);
            }
        }
    }
    fputc('"', stdout);
}

static void dump_view(const char *name, fb_app_t *app) {
    xm_view_t v;
    fb_app_render(app, &v);
    printf("{\"name\":");
    json_str(name);
    printf(",\"lines\":[");
    for (uint8_t i = 0; i < v.line_n; ++i) {
        const xm_view_line_t *l = &v.lines[i];
        if (i) fputc(',', stdout);
        printf("{\"x\":%d,\"y\":%d,\"w\":%u,\"size\":%u,\"color\":%u,\"align\":%u,\"text\":",
               l->x, l->y, (unsigned)l->w, (unsigned)l->size,
               (unsigned)l->color, (unsigned)l->align);
        json_str(l->text);
        fputc('}', stdout);
    }
    printf("],\"image\":{");
    if (v.image && v.image_w && v.image_h) {
        const char *nm = v.image_id < sizeof(k_scene_names) / sizeof(k_scene_names[0])
                             ? k_scene_names[v.image_id] : NULL;
        printf("\"name\":");
        json_str(nm);
        printf(",\"x\":%d,\"y\":%d,\"w\":%u,\"h\":%u",
               v.image_x, v.image_y, (unsigned)v.image_w, (unsigned)v.image_h);
    } else {
        printf("\"name\":null");
    }
    printf("},\"icons\":[");
    for (uint8_t i = 0; i < v.icon_n; ++i) {
        const xm_view_icon_t *ic = &v.icons[i];
        if (i) fputc(',', stdout);
        const char *nm = ic->id < sizeof(k_icon_names) / sizeof(k_icon_names[0])
                             ? k_icon_names[ic->id] : NULL;
        printf("{\"x\":%d,\"y\":%d,\"w\":%u,\"h\":%u,\"name\":",
               ic->x, ic->y, (unsigned)ic->w, (unsigned)ic->h);
        json_str(nm);
        fputc('}', stdout);
    }
    printf("],\"boxes\":[");
    for (uint8_t i = 0; i < v.box_n; ++i) {
        const xm_view_box_t *b = &v.boxes[i];
        if (i) fputc(',', stdout);
        printf("{\"x\":%d,\"y\":%d,\"w\":%u,\"h\":%u,\"color\":%u,\"fill\":%s,\"border\":%s}",
               b->x, b->y, (unsigned)b->w, (unsigned)b->h, (unsigned)b->color,
               b->fill ? "true" : "false", b->border ? "true" : "false");
    }
    printf("]}\n");
}

/* 用一个固定种子把游戏推进到指定画面，保证预览可复现 */
static fb_app_t g_app;

static void press(xm_input_t in) { fb_app_on_key(&g_app, in); }

int main(void) {
    fb_app_init(&g_app, 20261115u);

    dump_view("01_intro", &g_app);

    /* 封面选“攒了点钱”再进首页 */
    press(XM_IN_DOWN_CLICK);
    press(XM_IN_OK_CLICK);
    dump_view("02_home", &g_app);

    /* 首页 → 去干活 → 职业线 */
    press(XM_IN_OK_CLICK);
    dump_view("03_lines", &g_app);

    /* 选摆摊 → 进货页 */
    press(XM_IN_OK_CLICK);
    dump_view("04_market", &g_app);

    /* 连点确定进 6 份货，再选到“出摊卖货” */
    press(XM_IN_OK_CLICK);
    press(XM_IN_OK_CLICK);
    press(XM_IN_OK_CLICK);
    press(XM_IN_OK_CLICK);
    press(XM_IN_OK_CLICK);
    press(XM_IN_OK_CLICK);
    for (int i = 0; i < 5; ++i) press(XM_IN_DOWN_CLICK);
    dump_view("05_market_ready", &g_app);

    /* 出摊 → 收工报告 */
    press(XM_IN_OK_CLICK);
    dump_view("06_report", &g_app);

    /* 报告 → 若当天有事件则事件页；否则推进到次日首页 */
    press(XM_IN_OK_CLICK);
    if (g_app.scr == FB_SCR_EVENT) {
        dump_view("07_event", &g_app);
        press(XM_IN_OK_CLICK);
    }
    dump_view("08_next_home", &g_app);

    /* 玩法说明 */
    fb_app_init(&g_app, 20261115u);
    press(XM_IN_OK_CLICK);   /* 进首页 */
    for (int i = 0; i < 5; ++i) press(XM_IN_DOWN_CLICK);   /* 到“玩法说明” */
    press(XM_IN_OK_CLICK);
    dump_view("09_about", &g_app);

    /* 结局页：直接把状态推到结束 */
    fb_app_init(&g_app, 20261115u);
    press(XM_IN_OK_CLICK);
    g_app.st.ending = FB_END_SETTLE;
    g_app.st.day = FB_TOTAL_DAYS;
    g_app.st.cash = 15300;
    g_app.scr = FB_SCR_ENDING;
    dump_view("10_ending", &g_app);

    return 0;
}
