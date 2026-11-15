/* 折叠北京 · 页面与布局契约测试（host，不含 LVGL）。
 * 守住三类回归：
 *   1. 视图容量：lines <= 24、boxes <= 32，否则真机会丢行
 *   2. 屏幕边界：任何一行文本不得越出 240x320，也不得互相压字
 *   3. 导航闭环：封面→主面板→职业线→动作→收工→事件/次日，长按能退回
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fb_app.h"

static int s_fail;

static void check_bounds(const char *page, const xm_view_t *v) {
    if (v->line_n > XM_VIEW_LINE_MAX) {
        printf("  FAIL %s: line_n=%u 超过上限\n", page, v->line_n);
        ++s_fail;
    }
    if (v->box_n > XM_VIEW_BOX_MAX) {
        printf("  FAIL %s: box_n=%u 超过上限\n", page, v->box_n);
        ++s_fail;
    }
    /* 场景图不得越出 240x320，也不得压到状态条 */
    if (v->image && v->image_w && v->image_h) {
        if (v->image_x < 0 || v->image_y < 0 ||
            v->image_x + (int)v->image_w > 240 ||
            v->image_y + (int)v->image_h > 320) {
            printf("  FAIL %s: 场景图越界 (%d,%d %ux%u)\n", page, v->image_x,
                   v->image_y, v->image_w, v->image_h);
            ++s_fail;
        }
    }
    for (uint8_t i = 0; i < v->icon_n; ++i) {
        const xm_view_icon_t *ic = &v->icons[i];
        if (!ic->data || ic->w == 0u || ic->h == 0u) {
            printf("  FAIL %s: 图标 %u 为空\n", page, i);
            ++s_fail;
            continue;
        }
        if (ic->x < 0 || ic->y < 0 || ic->x + (int)ic->w > 240 ||
            ic->y + (int)ic->h > 320) {
            printf("  FAIL %s: 图标 %u 越界 (%d,%d)\n", page, i, ic->x, ic->y);
            ++s_fail;
        }
    }
    for (uint8_t i = 0; i < v->line_n; ++i) {
        const xm_view_line_t *l = &v->lines[i];
        if (!l->text || l->text[0] == '\0') continue;
        if (l->y < 0 || (int)l->y + (int)l->size > 320) {
            printf("  FAIL %s: 行越界 y=%d size=%u text=%s\n", page, l->y,
                   l->size, l->text);
            ++s_fail;
        }
        if (l->x < 0 || (int)l->x + (int)l->w > 240) {
            printf("  FAIL %s: 横向越界 x=%d w=%u text=%s\n", page, l->x, l->w,
                   l->text);
            ++s_fail;
        }
        /* 同一页内，后一行不得压到前一行（用 y+size 判，label 另有 6px 内边距） */
        for (uint8_t j = i + 1; j < v->line_n; ++j) {
            const xm_view_line_t *m = &v->lines[j];
            if (!m->text || m->text[0] == '\0') continue;
            int a1 = l->y, a2 = l->y + l->size;
            int b1 = m->y, b2 = m->y + m->size;
            if (a1 < b2 && b1 < a2) {
                /* 允许同 y 的左右两栏（x 不重叠即可） */
                int la1 = l->x, la2 = l->x + l->w;
                int lb1 = m->x, lb2 = m->x + m->w;
                if (la1 < lb2 && lb1 < la2) {
                    printf("  FAIL %s: 行重叠 [%s] 与 [%s] (y=%d/%d)\n", page,
                           l->text, m->text, l->y, m->y);
                    ++s_fail;
                }
            }
        }
    }
}

static void snap(const char *name, fb_app_t *app) {
    xm_view_t v;
    fb_app_render(app, &v);
    check_bounds(name, &v);
    printf("  %-14s lines=%2u boxes=%2u icons=%2u img=%s\n", name, v.line_n,
           v.box_n, v.icon_n, v.image ? "yes" : "no");
}

static void press(fb_app_t *app, xm_input_t in) { fb_app_on_key(app, in); }

int main(void) {
    printf("test_fb_app_pages\n");
    fb_app_t app;

    fb_app_init(&app, 20261115u);
    snap("intro", &app);

    /* 封面：背景可选 0..2，不能越界 */
    press(&app, XM_IN_DOWN_CLICK);
    press(&app, XM_IN_DOWN_CLICK);
    press(&app, XM_IN_DOWN_CLICK);
    assert(app.bg_sel == 2);

    press(&app, XM_IN_UP_CLICK);
    press(&app, XM_IN_UP_CLICK);
    assert(app.bg_sel == 0);

    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_HOME);
    snap("home", &app);

    /* 主菜单走到底再回到头 */
    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_LINES);
    snap("lines", &app);

    /* 长按退回主面板 */
    press(&app, XM_IN_OK_LONG);
    assert(app.scr == FB_SCR_HOME);

    /* 借钱页：主面板第 3 项 */
    press(&app, XM_IN_DOWN_CLICK);
    press(&app, XM_IN_DOWN_CLICK);
    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_FINANCE);
    snap("finance", &app);
    press(&app, XM_IN_OK_LONG);
    assert(app.scr == FB_SCR_HOME);
    assert(app.sel == FB_MENU_FINANCE);   /* 返回后光标要停在原来那一项 */

    /* 备货页：再下一项 */
    press(&app, XM_IN_DOWN_CLICK);
    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_STORAGE);
    snap("storage", &app);
    press(&app, XM_IN_OK_LONG);

    /* 玩法说明 */
    app.sel = FB_MENU_ABOUT;
    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_ABOUT);
    snap("about", &app);
    press(&app, XM_IN_OK_LONG);
    assert(app.scr == FB_SCR_HOME);

    /* 走一遍摆摊：进货 6 份 → 出摊 → 收工 */
    app.sel = FB_MENU_WORK;
    press(&app, XM_IN_OK_CLICK);        /* lines */
    app.sel = FB_LINE_MARKET;
    press(&app, XM_IN_OK_CLICK);        /* action */
    assert(app.scr == FB_SCR_ACTION);
    snap("market", &app);
    for (int i = 0; i < 6; ++i) press(&app, XM_IN_OK_CLICK);
    assert(app.st.inv_total == 6);
    app.sel = FB_KIND_COUNT;            /* "出摊卖货" */
    snap("market_ready", &app);
    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_REPORT);
    assert(app.st.day_earned > 0);
    snap("report", &app);

    /* 收工后：有事件则进事件页，否则直接次日 */
    press(&app, XM_IN_OK_CLICK);
    if (app.scr == FB_SCR_EVENT) {
        snap("event", &app);
        assert(app.sel == 0);
        press(&app, XM_IN_OK_CLICK);
    }
    assert(app.scr == FB_SCR_HOME || app.scr == FB_SCR_ENDING);
    snap("next", &app);

    /* 结局页 */
    fb_app_init(&app, 20261115u);
    press(&app, XM_IN_OK_CLICK);
    app.st.ending = FB_END_SETTLE;
    app.st.day = FB_TOTAL_DAYS;
    app.st.cash = 15300;
    app.scr = FB_SCR_ENDING;
    snap("ending", &app);

    /* 结局页按确定 → 重开回封面 */
    press(&app, XM_IN_OK_CLICK);
    assert(app.scr == FB_SCR_INTRO);

    if (s_fail) {
        printf("test_fb_app_pages: FAIL (%d)\n", s_fail);
        return 1;
    }
    printf("test_fb_app_pages: PASS\n");
    return 0;
}
