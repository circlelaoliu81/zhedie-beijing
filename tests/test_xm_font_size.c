/* test_xm_font_size.c —— 内容页字号 16px 防回归门禁。
 *
 * 背景：用户真机反馈「内容页字号太小，不好看清」。此前黄历三页、六爻结果、
 * 关于、历史、免责正文全是 12px，且各测试只检查「不超宽、不越界」，
 * 从不检查字面大小 —— 只要有人把 XM_FONT_BODY 改回 12 或新加一行裸 12u，
 * 全套门禁照样全绿。本测试把「正文最小字号」变成硬约束。
 *
 * 允许 12px 的只剩顶栏状态条（y<22，异常条也要挤进 172px）；
 * 所有 y>=50 的上屏文字一律 >=16px，标题本来就是 20/24。
 * 同时逐行复验 approx_width(text,size) <= w，防止放大字号后静默裁字。
 *
 * 负向对照：把 xm_vm.h 的 XM_FONT_BODY 从 16u 改回 12u，本测试必须 FAIL。
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_fontcov.h"
#include "xm_huangli_data.h"
#include "xm_layout.h"
#include "xm_liuyao.h"
#include "xm_nav.h"
#include "xm_pages.h"
#include "xm_store.h"
#include "xm_time.h"
#include "xm_vm.h"
#include "xm_yijing.h"

static xm_hl_table_t g_hl;
static uint32_t g_lines_checked;
static uint32_t g_body_lines;

/* 判据必须写死，绝不能引用 XM_FONT_BODY：否则把宏改回 12u 时，
 * 期望值会跟着降，测试永远 PASS（第一次负向对照就栽在这里）。 */
#define REQUIRED_BODY_PX 16u

static uint16_t approx_width(const char *text, uint8_t size) {
    uint16_t w = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (p && *p) {
        const uint8_t len = xm_utf8_char_len(*p);
        if (len == 0u) break;
        w = (uint16_t)(w + (*p < 0x80u ? (uint16_t)((size / 2u) + 1u)
                                        : (uint16_t)size));
        p += len;
    }
    return w;
}

static void check_view(const xm_view_t *v, int page) {
    assert(xm_layout_assert_footer(v));
    for (uint8_t i = 0; i < v->line_n && i < 24u; ++i) {
        const xm_view_line_t *l = &v->lines[i];
        assert(l->text != NULL);
        const uint16_t px = approx_width(l->text, l->size);
        if (px > l->w) {
            printf("  FAIL: page=%d y=%d size=%u 文本 %u px 超出 w=%u: %s\n",
                   page, (int)l->y, (unsigned)l->size, (unsigned)px,
                   (unsigned)l->w, l->text);
        }
        assert(px <= l->w);
        if (l->y >= 50) {
            if (l->size < REQUIRED_BODY_PX) {
                printf("  FAIL: page=%d y=%d 正文退回 %upx（要求 >=%upx）: %s\n",
                       page, (int)l->y, (unsigned)l->size,
                       (unsigned)REQUIRED_BODY_PX, l->text);
            }
            assert(l->size >= REQUIRED_BODY_PX);
            g_body_lines++;
        }
        g_lines_checked++;
    }
}

/* 1. 全部页面走真实 xm_pages_render（含黄历查表、六爻、历史、关于）。 */
static void test_all_pages_real_state(void) {
    xm_time_t time;
    xm_time_init(&time);
    xm_store_t store;
    xm_store_init(&store);
    xm_liuyao_t ly;
    memset(&ly, 0, sizeof(ly));
    uint8_t toss[6] = {0};

    for (int p = 0; p < (int)XM_PAGE_COUNT; ++p) {
        xm_nav_t nav;
        xm_nav_init(&nav);
        nav.disclaimer_acked = true;   /* 首启未确认免责会被强制拉回免责页 */
        nav.page = (xm_page_t)p;
        const xm_view_t v = xm_pages_render(&nav, &time, &store, &ly, toss,
                                            &g_hl, 0, 87, 0);
        check_view(&v, p);
    }
    printf("  全部 %d 页真实状态：逐行不超宽、y>=50 正文 >=%upx: PASS\n",
           (int)XM_PAGE_COUNT, (unsigned)REQUIRED_BODY_PX);
}

/* 2. 最坏长文本：结果页 8 条超长文案 + 黄历三页 14 行满行，
 *    放大字号后折行更多，必须仍不越界、不超宽、正文仍是 16px。 */
static void test_worst_case_text(void) {
    static const char *const s_result[8] = {
        "本卦：归妹",
        "之卦：损 · 静卦无动",
        "卦辞：征凶，无攸利。",
        "上爻 上六：女承筐，无实；士刲羊，无血。无攸利。",
        "变卦卦辞：有孚，元吉，无咎，可贞，利有攸往。曷之用？二簋可用享。",
        "断语：少女出嫁，迟缓则凶。归妹为嫁女之象，急于求成必失其序。",
        "大象：泽上有雷，归妹。君子以永终知敝。",
        "初爻 初九：归妹以娣，跛能履，征吉。",
    };
    xm_page_state_t st;
    xm_vm_state_init(&st);
    st.result_line_n = 8u;
    for (uint8_t i = 0; i < 8u; ++i) {
        (void)snprintf(st.result_lines[i], sizeof(st.result_lines[i]), "%s", s_result[i]);
    }
    const xm_view_t result = xm_vm_render(XM_PAGE_LY_RESULT, &st);
    check_view(&result, (int)XM_PAGE_LY_RESULT);

    xm_vm_state_init(&st);
    st.hl_line_n = 14u;
    st.hl_head_n = 5u;
    st.hl_col_split = 10u;
    for (uint8_t i = 0; i < 14u; ++i) {
        (void)snprintf(st.hl_lines[i], sizeof(st.hl_lines[i]), "宜：祭祀 %u", (unsigned)i);
    }
    const xm_page_t pages[3] = {XM_PAGE_HL_A, XM_PAGE_HL_B, XM_PAGE_HL_C};
    for (uint8_t i = 0; i < 3u; ++i) {
        const xm_view_t v = xm_vm_render(pages[i], &st);
        check_view(&v, (int)pages[i]);
    }
    printf("  结果页超长文案 + 黄历 14 行满行：仍不超宽不越界、正文 >=%upx: PASS\n",
           (unsigned)REQUIRED_BODY_PX);
}

int main(void) {
    printf("test_xm_font_size\n");
    assert(xm_hl_init(&g_hl, xm_huangli_blob, xm_huangli_blob_size) == XM_OK);
    (void)xm_yijing_init();
    test_all_pages_real_state();
    test_worst_case_text();
    /* 自检：门禁必须真的检查了足够多的正文行，防止「空跑永远 PASS」的假绿。 */
    assert(g_lines_checked >= 80u);
    assert(g_body_lines >= 40u);
    printf("  共核 %u 行（其中正文 %u 行）\n",
           (unsigned)g_lines_checked, (unsigned)g_body_lines);
    printf("test_xm_font_size: PASS\n");
    return 0;
}
