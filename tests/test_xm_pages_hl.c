/* test_xm_pages_hl.c —— 黄历三张子页的内容完整性 host 测试。
 *
 * 保护的是一类「静默丢内容」缺陷：曾经 hl_lines[] 不清零 + 前缀靠「缓冲是否为空」判断，
 * 导致
 *   · 「宜：」「忌：」「吉神：」「凶煞：」四个分区标签全部消失；
 *   · 「宜：嫁娶…」被拼到残留的时辰行后面，屏幕上出现「辰 壬辰 · 凶 嫁娶 祭祀…」；
 *   · 行数被占满后，「忌」/「凶煞」整段一条都不显示。
 * 这些都不会报错、不会崩，只会让用户少看到一半内容，所以必须靠测试盯住。
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_huangli_data.h"
#include "xm_liuyao.h"
#include "xm_nav.h"
#include "xm_pages.h"
#include "xm_store.h"
#include "xm_time.h"
#include "xm_vm.h"
#include "xm_yijing.h"

#define LINE_MAX 24

static xm_hl_table_t g_hl;

typedef struct {
    char lines[LINE_MAX][160];
    uint8_t count;
} dump_t;

static dump_t render_page(xm_page_t page) {
    xm_nav_t nav;
    xm_nav_init(&nav);
    nav.page = page;

    xm_time_t time;
    xm_time_init(&time);
    xm_store_t store;
    xm_store_init(&store);
    xm_liuyao_t ly;
    memset(&ly, 0, sizeof(ly));
    uint8_t toss[6] = {0};

    const xm_view_t view = xm_pages_render(&nav, &time, &store, &ly, toss,
                                           &g_hl, 0, 87, 0);
    dump_t out;
    memset(&out, 0, sizeof(out));
    for (uint8_t i = 0; i < view.line_n && i < LINE_MAX; ++i) {
        (void)snprintf(out.lines[out.count], sizeof(out.lines[0]), "%s", view.lines[i].text);
        out.count++;
    }
    return out;
}

static uint8_t count_prefix(const dump_t *d, const char *prefix) {
    uint8_t n = 0;
    const size_t pl = strlen(prefix);
    for (uint8_t i = 0; i < d->count; ++i) {
        if (strncmp(d->lines[i], prefix, pl) == 0) n++;
    }
    return n;
}

/* 时辰行形如「辰 壬辰 · 凶」/「巳 癸巳 · 凶 当前」：出现在 HL_A/HL_B 就说明串页了。 */
static uint8_t count_hour_pollution(const dump_t *d) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < d->count; ++i) {
        if (strstr(d->lines[i], "· 吉") != NULL || strstr(d->lines[i], "· 凶") != NULL) n++;
    }
    return n;
}

/* 「宜： 嫁娶」这种冒号后多一个空格的排版瑕疵也要挡住。 */
static uint8_t count_colon_gap(const dump_t *d) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < d->count; ++i) {
        if (strstr(d->lines[i], "： ") != NULL) n++;
    }
    return n;
}

static void test_hl_a_sections(void) {
    const dump_t d = render_page(XM_PAGE_HL_A);
    const uint8_t yi = count_prefix(&d, "宜：");
    const uint8_t ji = count_prefix(&d, "忌：");
    assert(yi >= 1u);
    assert(ji >= 1u);                          /* 「忌」整段不许消失 */
    assert(count_hour_pollution(&d) == 0u);    /* 不许串进时辰行 */
    assert(count_colon_gap(&d) == 0u);
    printf("  HL_A 宜 %u 行 / 忌 %u 行，无串页: PASS\n", (unsigned)yi, (unsigned)ji);
}

static void test_hl_b_sections(void) {
    const dump_t d = render_page(XM_PAGE_HL_B);
    const uint8_t js = count_prefix(&d, "吉神：");
    const uint8_t xs = count_prefix(&d, "凶煞：");
    assert(js >= 1u);
    assert(xs >= 1u);                          /* 「凶煞」整段不许消失 */
    assert(count_hour_pollution(&d) == 0u);
    assert(count_colon_gap(&d) == 0u);
    printf("  HL_B 吉神 %u 行 / 凶煞 %u 行，无串页: PASS\n", (unsigned)js, (unsigned)xs);
}

static void test_hl_c_hours(void) {
    const dump_t d = render_page(XM_PAGE_HL_C);
    /* 16px 两栏版式把时辰行压成「子 戊子 吉」（去掉了原先的 · 与「当前」后缀，
     * 「当前」改由朱砂色 + 说明行表达），所以按十二地支行首计数，不再依赖旧标点。 */
    static const char *const names[12] = {
        "子 ", "丑 ", "寅 ", "卯 ", "辰 ", "巳 ",
        "午 ", "未 ", "申 ", "酉 ", "戌 ", "亥 "
    };
    uint8_t hours = 0;
    for (uint8_t h = 0; h < 12u; ++h) {
        if (count_prefix(&d, names[h]) >= 1u) hours++;
    }
    assert(hours == 12u);                      /* 十二时辰一条不少 */
    printf("  HL_C 十二时辰 %u 行: PASS\n", (unsigned)hours);
}

/* 反复渲染不同页：上一帧的残留不得影响下一帧（就是本次 bug 的触发方式）。 */
static void test_no_cross_page_contamination(void) {
    (void)render_page(XM_PAGE_HL_C);           /* 先把 hl_lines 灌满时辰行 */
    const dump_t a = render_page(XM_PAGE_HL_A);
    assert(count_prefix(&a, "宜：") >= 1u);
    assert(count_prefix(&a, "忌：") >= 1u);
    assert(count_hour_pollution(&a) == 0u);

    (void)render_page(XM_PAGE_HL_A);           /* 再灌满宜忌 */
    const dump_t b = render_page(XM_PAGE_HL_B);
    assert(count_prefix(&b, "吉神：") >= 1u);
    assert(count_prefix(&b, "凶煞：") >= 1u);
    assert(strstr(b.lines[0], "宜：") == NULL);
    printf("  HL_C→HL_A→HL_B 交叉渲染无残留: PASS\n");
}

/* 授时页接线：上/下键改的是 nav->draft_date，屏上 5 段数字必须同步跟着变。
 * 旧版 xm_pages.c 把这 5 段从 xm_time_now() 取，状态机改了草稿、屏幕纹丝不动，
 * 真机表现就是「授时按上下键时间无法调整」；而旧测试只断言 nav.draft_date 变化，
 * 从不检查 xm_view_t，才让这个接线漏点一路假绿。 */
static void render_text(const xm_view_t *v, char *out, size_t cap) {
    out[0] = '\0';
    for (uint8_t i = 0; i < v->line_n; ++i) {
        (void)strncat(out, v->lines[i].text, cap - strlen(out) - 1u);
        (void)strncat(out, "|", cap - strlen(out) - 1u);
    }
}

static void test_time_page_shows_editable_draft(void) {
    xm_nav_t nav;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;   /* 首启未确认免责时导航会强制拉回免责页 */
    nav.page = XM_PAGE_TIME;
    nav.draft_date.year = 2030;
    nav.draft_date.month = 3;
    nav.draft_date.day = 7;
    nav.draft_date.hour = 9;
    nav.draft_date.minute = 45;

    xm_time_t time;
    xm_time_init(&time);      /* 系统时钟 2026-11-13 10:30，与草稿刻意不同 */
    xm_store_t store;
    xm_store_init(&store);
    xm_liuyao_t ly;
    memset(&ly, 0, sizeof(ly));
    uint8_t toss[6] = {0};
    char all[1024];

    const xm_view_t v = xm_pages_render(&nav, &time, &store, &ly, toss, &g_hl, 0, 87, 0);
    render_text(&v, all, sizeof(all));
    assert(strstr(all, "2030") != NULL);   /* 屏上必须是正在编辑的草稿 */
    assert(strstr(all, "03") != NULL);
    assert(strstr(all, "07") != NULL);
    assert(strstr(all, "09") != NULL);
    assert(strstr(all, "45") != NULL);
    assert(strstr(all, "2026") == NULL);   /* 不许偷偷显示系统时钟年份 */
    assert(strstr(all, "10") == NULL);

    /* 真按一次上键：草稿 +1，屏上数字必须同步 +1。 */
    xm_nav_result_t out;
    assert(xm_nav_dispatch(&nav, XM_IN_UP_CLICK, 0, &out) == XM_OK);
    assert(nav.draft_date.year == 2031);
    const xm_view_t v2 = xm_pages_render(&nav, &time, &store, &ly, toss, &g_hl, 0, 87, 0);
    render_text(&v2, all, sizeof(all));
    assert(strstr(all, "2031") != NULL);
    assert(strstr(all, "2030") == NULL);
    printf("  授时页屏上数字来自草稿、按上下键即时变化: PASS\n");
}

int main(void) {
    printf("test_xm_pages_hl\n");
    assert(xm_hl_init(&g_hl, xm_huangli_blob, xm_huangli_blob_size) == XM_OK);
    (void)xm_yijing_init();
    test_hl_a_sections();
    test_hl_b_sections();
    test_hl_c_hours();
    test_no_cross_page_contamination();
    test_time_page_shows_editable_draft();
    printf("test_xm_pages_hl: PASS\n");
    return 0;
}
