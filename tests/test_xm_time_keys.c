/* test_xm_time_keys.c —— 授时页键位契约。
 *
 * 缺陷背景：旧版把「切字段」绑在双击上/下键，屏上却写「确定键选定」；
 * 而 iot_button 会把快速连按合并成 DOUBLE_CLICK，用户连按调年份时
 * 数值没变、字段反而跳走 —— 真机表现为「授时无法操作、键位不对」。
 * 本测试同时守住：连按归一、屏上文案、5 段推进、最后一段完成、从设置长按返回。
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_input_map.h"
#include "xm_layout.h"
#include "xm_nav.h"
#include "xm_strings.h"
#include "xm_vm.h"

static const xm_view_line_t *find_line(const xm_view_t *v, int16_t y) {
    for (uint8_t i = 0; i < v->line_n; ++i) {
        if (v->lines[i].y == y) return &v->lines[i];
    }
    return NULL;
}

static void test_double_and_multiple_are_one_click(void) {
    assert(xm_input_map_pure(XM_KEY_UP, XM_KEY_EV_DOUBLE) == XM_IN_UP_CLICK);
    assert(xm_input_map_pure(XM_KEY_DOWN, XM_KEY_EV_DOUBLE) == XM_IN_DOWN_CLICK);
    assert(xm_input_map_pure(XM_KEY_OK, XM_KEY_EV_DOUBLE) == XM_IN_OK_CLICK);
    assert(xm_input_map_pure(XM_KEY_UP, XM_KEY_EV_MULTIPLE) == XM_IN_UP_CLICK);
    /* 抬起 / 按下永远不是指令；上/下键长按也没有全局语义。 */
    assert(xm_input_map_pure(XM_KEY_UP, XM_KEY_EV_RELEASE) == XM_IN_NONE);
    assert(xm_input_map_pure(XM_KEY_OK, XM_KEY_EV_PRESS) == XM_IN_NONE);
    assert(xm_input_map_pure(XM_KEY_UP, XM_KEY_EV_LONG) == XM_IN_NONE);
    assert(xm_input_map_pure(XM_KEY_OK, XM_KEY_EV_LONG) == XM_IN_OK_LONG);
    printf("  连按归一为一次单击、抬起不是指令: PASS\n");
}

static void test_up_down_adjust_value_not_cursor(void) {
    xm_nav_t nav;
    xm_nav_result_t out;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_TIME;

    assert(xm_nav_dispatch(&nav, XM_IN_UP_CLICK, 0, &out) == XM_OK);
    assert(nav.draft_date.year == 2027);
    assert(nav.time_cursor == 0u);          /* 上下键只调数值，不许偷偷换字段 */
    assert(out.action == XM_NAV_ACTION_RENDER);
    assert(xm_nav_dispatch(&nav, XM_IN_DOWN_CLICK, 0, &out) == XM_OK);
    assert(nav.draft_date.year == 2026);
    assert(nav.time_cursor == 0u);

    /* 连按（已被归一成 CLICK）必须连续改变数值，而不是跳字段。 */
    for (int i = 0; i < 5; ++i) {
        assert(xm_nav_dispatch(&nav, XM_IN_UP_CLICK, 0, &out) == XM_OK);
    }
    assert(nav.draft_date.year == 2031);
    assert(nav.time_cursor == 0u);
    printf("  上下键连续调数值、字段不乱跳: PASS\n");
}

static void test_ok_advances_five_fields_then_confirms(void) {
    xm_nav_t nav;
    xm_nav_result_t out;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_TIME;

    for (uint8_t i = 0; i < 4u; ++i) {
        assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 0, &out) == XM_OK);
        assert(nav.page == XM_PAGE_TIME);
        assert(nav.time_cursor == (uint8_t)(i + 1u));
        assert(!out.persist_time);
        assert(out.action == XM_NAV_ACTION_RENDER);
    }
    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_HOME);
    assert(nav.time_confirmed);
    assert(out.persist_time);
    printf("  确定键逐段推进 5 段、最后一段完成设定: PASS\n");
}

static void test_long_press_returns_only_from_settings(void) {
    xm_nav_t nav;
    xm_nav_result_t out;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_SETTINGS;
    nav.sel = 0u;
    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_TIME);
    assert(nav.depth > 0u);
    assert(xm_nav_dispatch(&nav, XM_IN_OK_LONG, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_SETTINGS);

    /* 首启引导进授时时没有上一页：不许长按跳过授时。 */
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_TIME;
    assert(xm_nav_dispatch(&nav, XM_IN_OK_LONG, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_TIME);
    printf("  长按确定键只从设置页返回、首启不许跳过授时: PASS\n");
}

static void test_screen_text_matches_key_mapping(void) {
    for (uint8_t cursor = 0u; cursor < 5u; ++cursor) {
        xm_page_state_t st;
        xm_vm_state_init(&st);
        st.page = XM_PAGE_TIME;
        st.time_cursor = cursor;
        snprintf(st.time_progress, sizeof(st.time_progress),
                 "第 %u / 5 段", (unsigned)(cursor + 1u));
        const xm_view_t v = xm_vm_render(XM_PAGE_TIME, &st);
        bool progress_ok = false;
        const char *const want = (cursor == 4u) ? XM_TIME_LAST : XM_TIME_HINT;
        for (uint8_t i = 0; i < v.line_n; ++i) {
            const xm_view_line_t *l = &v.lines[i];
            if (l->y == 168) {
                assert(l->size == 20u);
                assert(strstr(l->text, "第") != NULL);
                assert(strstr(l->text, "段") != NULL);
                assert(xm_layout_wrap(l->text, l->size, l->w) <= 1u);
                progress_ok = true;
            }
        }
        /* y=266 的页脚是键位口径的唯一出处：不许再写列表页的「选择/进入」。 */
        const xm_view_line_t *foot = find_line(&v, 266);
        assert(foot != NULL);
        assert(strcmp(foot->text, want) == 0);
        assert(strstr(foot->text, "上下键") != NULL);
        assert(strstr(foot->text, "选择") == NULL);
        assert(strstr(foot->text, "进入") == NULL);
        assert(strstr(foot->text, "确定键") != NULL);
        assert(xm_layout_wrap(foot->text, foot->size, foot->w) <= 1u);
        /* 页内不许再重复一遍同句键位提示：16px 下同屏两遍一模一样的大字很难看。 */
        for (uint8_t i = 0; i < v.line_n; ++i) {
            const xm_view_line_t *l = &v.lines[i];
            if (l->y >= 50 && l->y < 264) assert(strcmp(l->text, want) != 0);
        }
        assert(progress_ok);
        assert(xm_layout_assert_footer(&v));
    }
    printf("  授时页屏上提示与状态机键位一致、进度可见: PASS\n");
}

static void test_footer_hint_matches_each_page(void) {
    xm_page_state_t st;
    xm_vm_state_init(&st);

    /* 免责页 / 三键引导页：页内已有「按确定键确认」按钮与逐键说明，
     * 通用列表提示在这里是假的（没有可上下选择的列表），页脚必须也只认确定键。
     * 几何门禁要求每页按键区都有一行提示，故不能整行删掉。 */
    const xm_view_t disclaimer = xm_vm_render(XM_PAGE_DISCLAIMER, &st);
    const xm_view_line_t *d_foot = find_line(&disclaimer, 266);
    assert(d_foot != NULL);
    assert(strcmp(d_foot->text, XM_KEY_NEXT) == 0);
    const xm_view_t guide = xm_vm_render(XM_PAGE_KEYGUIDE, &st);
    const xm_view_line_t *g_foot = find_line(&guide, 266);
    assert(g_foot != NULL);
    assert(strcmp(g_foot->text, XM_KEY_NEXT) == 0);

    /* 摇卦页：页脚必须强化「每按一次确定键摇一爻」，不能是列表口径。 */
    st.page = XM_PAGE_LY_CAST;
    const xm_view_t cast = xm_vm_render(XM_PAGE_LY_CAST, &st);
    const xm_view_line_t *foot = find_line(&cast, 266);
    assert(foot != NULL);
    assert(strcmp(foot->text, XM_CAST_HINT) == 0);
    assert(strstr(foot->text, "选择") == NULL);
    assert(strstr(foot->text, "进入") == NULL);
    assert(xm_layout_wrap(foot->text, foot->size, foot->w) <= 1u);

    /* 列表页仍保留通用提示，别把菜单导航一并改坏。 */
    st.page = XM_PAGE_HOME;
    const xm_view_t home = xm_vm_render(XM_PAGE_HOME, &st);
    const xm_view_line_t *home_foot = find_line(&home, 266);
    assert(home_foot != NULL);
    assert(strcmp(home_foot->text, XM_KEY_HINT) == 0);
    printf("  各页页脚键位口径一致、免责/引导不再画假提示: PASS\n");
}

int main(void) {
    printf("test_xm_time_keys\n");
    test_double_and_multiple_are_one_click();
    test_up_down_adjust_value_not_cursor();
    test_ok_advances_five_fields_then_confirms();
    test_long_press_returns_only_from_settings();
    test_screen_text_matches_key_mapping();
    test_footer_hint_matches_each_page();
    printf("test_xm_time_keys: PASS\n");
    return 0;
}
