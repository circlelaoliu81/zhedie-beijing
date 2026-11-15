/* test_xm_vm_layout.c —— 页面几何门禁 / 长文本折行的 host 测试。
 *
 * 覆盖两个历史缺陷：
 *   1. xm_vm.c 曾用 (void)xm_layout_assert_footer(&view) 把门禁结果直接丢弃，
 *      导致 5 个页面把说明文字压进按键区（y>=264）也无人发现。
 *   2. 六爻结果页正文曾与卦象横线重叠、长句不折行被裁掉半句。
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_fontcov.h"
#include "xm_layout.h"
#include "xm_vm.h"

/* 与 xm_layout_wrap / xm_vm 折行相同的度量：CJK 宽 = size，ASCII 宽 = size/2 + 1。 */
static uint16_t approx_width(const char *text, uint8_t size) {
    uint16_t w = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (p && *p) {
        const uint8_t len = xm_utf8_char_len(*p);
        if (len == 0u) break;
        w = (uint16_t)(w + (*p < 0x80u ? (uint16_t)((size / 2u) + 1u) : (uint16_t)size));
        p += len;
    }
    return w;
}

/* 行首禁则：句读/收尾标点不得落在行首。 */
static bool starts_with_closing_punct(const char *text) {
    const char *p = text;
    const uint32_t cp = xm_utf8_decode(&p);
    switch (cp) {
        case 0x3002u: case 0x3001u: case 0xFF0Cu: case 0xFF1Bu: case 0xFF1Au:
        case 0xFF1Fu: case 0xFF01u: case 0xFF09u: case 0x300Bu: case 0x300Du:
        case 0x300Fu: case 0x201Du: case 0x2026u: case 0xFF05u:
            return true;
        default:
            return false;
    }
}

/* 1. 全部页面（空状态）都必须过几何门禁。 */
static void test_all_pages_pass_gate(void) {
    for (int p = 0; p < (int)XM_PAGE_COUNT; ++p) {
        xm_page_state_t st;
        xm_vm_state_init(&st);
        st.page = (xm_page_t)p;
        const xm_view_t v = xm_vm_render((xm_page_t)p, &st);
        assert(xm_layout_assert_footer(&v));
    }
    printf("  全部 %d 页空状态过门禁: PASS\n", (int)XM_PAGE_COUNT);
}

/* 2. 黄历页拉到满行（14 行）也不能压进按键区。 */
static void test_hl_full_lines_pass_gate(void) {
    xm_page_state_t st;
    xm_vm_state_init(&st);
    st.hl_line_n = 14u;
    for (uint8_t i = 0; i < 14u; ++i) {
        (void)snprintf(st.hl_lines[i], sizeof(st.hl_lines[i]),
                       "宜：祭祀 祈福 求嗣 开光 出行 拆卸 %u", (unsigned)i);
    }
    const xm_page_t pages[3] = {XM_PAGE_HL_A, XM_PAGE_HL_B, XM_PAGE_HL_C};
    for (uint8_t i = 0; i < 3u; ++i) {
        const xm_view_t v = xm_vm_render(pages[i], &st);
        assert(xm_layout_assert_footer(&v));
    }
    printf("  黄历三页 14 行满行过门禁: PASS\n");
}

/* 3. 六爻结果页最坏情况：8 条超长文本 → 折行、截断、不越界、不压底部说明。
 *    本轮结果页拆九屏，子屏标题在 y=30（20px 朱砂），正文整宽 y=52 起 8 视觉行
 *    （步距 19，末行 185，底 201），来源说明在 y=226/242，页脚键位条 y=266。 */
static void test_ly_result_worst_case(void) {
    static const char *const s_lines[8] = {
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
    st.page = XM_PAGE_LY_RESULT;
    st.result_line_n = 8u;
    for (uint8_t i = 0; i < 8u; ++i) {
        (void)snprintf(st.result_lines[i], sizeof(st.result_lines[i]), "%s", s_lines[i]);
    }
    const xm_view_t v = xm_vm_render(XM_PAGE_LY_RESULT, &st);
    assert(xm_layout_assert_footer(&v));

    uint8_t header = 0;
    uint8_t body = 0;
    for (uint8_t i = 0; i < v.line_n; ++i) {
        const xm_view_line_t *l = &v.lines[i];
        /* 正文/标题一律不许超宽（折行必须真的切开）。 */
        assert(approx_width(l->text, l->size) <= l->w);
        if (l->y == 30 && l->size == 20) header++;     /* 子屏标题 */
        if (l->y >= 52 && l->y <= 185 && l->size == XM_FONT_BODY) {
            body++;
            assert(!starts_with_closing_punct(l->text)); /* 句读不落行首 */
        }
        /* 正文视觉底不得压到 y=222 起的两行来源说明（来源本身恰在 y=222，豁免）。 */
        assert(!(l->y >= 200 && l->y < 222));
    }
    assert(header == 1u);
    assert(body >= 1u && body <= 8u);
    printf("  六爻结果页最坏情况：正文 %u 行、标题 1 行、全部不超宽不越界: PASS\n",
           (unsigned)body);
}

/* 4. 引导页只抑制 E1，其它异常（E3/E5 等）照旧显示。 */
static void test_keyguide_overlay(void) {
    xm_page_state_t st;
    xm_vm_state_init(&st);
    st.page = XM_PAGE_KEYGUIDE;

    st.err_flags = XM_ERR_TIME_UNCALIBRATED_BIT;
    xm_view_t v = xm_vm_render(XM_PAGE_KEYGUIDE, &st);
    assert(v.overlay == 0u);

    st.err_flags = (uint8_t)(XM_ERR_TIME_UNCALIBRATED_BIT | XM_ERR_NVS_DEGRADED_BIT);
    v = xm_vm_render(XM_PAGE_KEYGUIDE, &st);
    assert(v.overlay == XM_ERR_NVS_DEGRADED_BIT);
    printf("  引导页抑制 E1、保留 E3: PASS\n");
}

/* 5. 引导页文案必须能在 224px 内单行放下（含空格）。 */
static void test_keyguide_lines_fit(void) {
    xm_page_state_t st;
    xm_vm_state_init(&st);
    const xm_view_t v = xm_vm_render(XM_PAGE_KEYGUIDE, &st);
    uint8_t guide_lines = 0;
    for (uint8_t i = 0; i < v.line_n; ++i) {
        const xm_view_line_t *l = &v.lines[i];
        assert(approx_width(l->text, l->size) <= l->w);
        if (l->y >= 44 && l->y <= 240) guide_lines++;
    }
    assert(guide_lines >= 5u);
    printf("  引导页 %u 行文案均单行可容: PASS\n", (unsigned)guide_lines);
}

/* 6. 异常状态条不许压住任何文字。
 *
 * 历史缺陷：红条画在 (8,24)-(232,44)，而 HOME / HL 三页 / LY_CAST / LY_HISTORY /
 * SETTINGS / ABOUT 的标题都在 y=30~34（size 20~24），整行被盖掉 70%。
 * 现在红条挪进顶栏状态区 (8,2) 176×18，与右侧电量并排；VM 在有异常时不输出
 * 顶栏「玄门」字样（那块被状态条占了），页面大标题一个像素没动。
 * 这里守住三件事：异常文案放得进 172px 标签、顶栏状态区不再有文字、
 * 异常态下页面大标题仍然存在（不许被顺手删掉）。 */
#define STATUS_ZONE_H 22u

static void test_overlay_banner_fits_status_zone(void) {
    static const uint8_t flags[] = {
        XM_ERR_TIME_UNCALIBRATED_BIT,   /* E1 */
        XM_ERR_HL_EXPIRED_BIT,          /* E2 */
        XM_ERR_NVS_DEGRADED_BIT,        /* E3 */
        XM_ERR_CONTENT_MISSING_BIT,     /* E5 */
    };
    /* 与 main/xm_status.c 的 bar(8,2,176,18) / label(2,2,172,14) 一致。 */
    const uint16_t label_w = 172u;
    for (uint8_t f = 0; f < (uint8_t)(sizeof(flags) / sizeof(flags[0])); ++f) {
        const char *text = xm_vm_overlay_text(flags[f]);
        assert(text && text[0] != '\0');
        assert(approx_width(text, 12) <= label_w);

        xm_page_state_t st;
        xm_vm_state_init(&st);
        st.err_flags = flags[f];
        for (int p = 0; p < (int)XM_PAGE_COUNT; ++p) {
            const xm_view_t v = xm_vm_render((xm_page_t)p, &st);
            /* 顶栏状态区被状态条占用：overlay 非 0 时不许再有 y<22 的文字。 */
            if (v.overlay != 0u) {
                for (uint8_t i = 0; i < v.line_n; ++i) {
                    assert(v.lines[i].y >= (int16_t)STATUS_ZONE_H);
                }
            }
            assert(xm_layout_assert_footer(&v));
        }
    }
    printf("  异常条 4 条文案均 <= %upx，且顶栏状态区不再有文字: PASS\n",
           (unsigned)label_w);
}

/* 6b. 异常态下 7 个模板的页面大标题必须还在（不许为了躲红条把标题删了）。 */
static void test_overlay_keeps_page_titles(void) {
    static const uint8_t flags[] = {
        XM_ERR_TIME_UNCALIBRATED_BIT, XM_ERR_HL_EXPIRED_BIT,
        XM_ERR_NVS_DEGRADED_BIT, XM_ERR_CONTENT_MISSING_BIT,
    };
    /* 这 7 个模板在正常态下的标题 y 坐标（见 xm_vm.c 各 case）。 */
    static const xm_page_t titled[7] = {
        XM_PAGE_HOME, XM_PAGE_HL_A, XM_PAGE_HL_B, XM_PAGE_HL_C,
        XM_PAGE_LY_CAST, XM_PAGE_LY_HISTORY, XM_PAGE_SETTINGS
    };
    for (uint8_t f = 0; f < (uint8_t)(sizeof(flags) / sizeof(flags[0])); ++f) {
        xm_page_state_t normal;
        xm_vm_state_init(&normal);
        for (uint8_t i = 0; i < 7u; ++i) {
            const xm_view_t vn = xm_vm_render(titled[i], &normal);
            xm_page_state_t st;
            xm_vm_state_init(&st);
            st.err_flags = flags[f];
            const xm_view_t vo = xm_vm_render(titled[i], &st);
            /* 标题行数必须一致：异常态只是把条子挪到顶栏，不许顺带删标题。
             * 正常态比异常态多一行的是顶栏「玄门」（y=4，被状态条占用了），
             * 所以只比较 y>=22 的正文/标题行数。 */
            uint8_t normal_body = 0u;
            uint8_t overlay_body = 0u;
            for (uint8_t i = 0; i < vn.line_n; ++i) {
                if (vn.lines[i].y >= 22) normal_body++;
            }
            for (uint8_t i = 0; i < vo.line_n; ++i) {
                if (vo.lines[i].y >= 22) overlay_body++;
            }
            assert(overlay_body == normal_body);
            /* 标题本身必须在：HOME 标题 y=34、HL 三页 y=30、LY_CAST/HISTORY y=32、
             * SETTINGS y=30。异常态下这些标题一个都不许少。 */
            bool has_title = false;
            for (uint8_t i = 0; i < vo.line_n; ++i) {
                if (vo.lines[i].y >= 30 && vo.lines[i].y <= 34) has_title = true;
            }
            assert(has_title);
        }
    }
    printf("  异常态下 7 个模板的标题行数与正常态一致: PASS\n");
}

/* 7. 授时页字段高亮：5 个字段各高亮一次，必须真的换位置（以前整行一片红，看不出在调谁）。 */
static void test_time_cursor_highlight(void) {
    static const char *const expected[5] = {"2026", "11", "13", "10", "30"};
    int16_t last_x = -1;
    for (uint8_t cursor = 0; cursor < 5u; ++cursor) {
        xm_page_state_t st;
        xm_vm_state_init(&st);
        st.page = XM_PAGE_TIME;
        st.time_cursor = cursor;
        const xm_view_t v = xm_vm_render(XM_PAGE_TIME, &st);

        /* 恰好一个字段是朱砂色，并且它就是要编辑的那一个。 */
        uint8_t red_fields = 0;
        int16_t red_x = -1;
        for (uint8_t i = 0; i < v.line_n; ++i) {
            const xm_view_line_t *l = &v.lines[i];
            if (l->y != 84 && l->y != 118) continue;
            if (l->color == XM_COLOR_RED) {
                red_fields++;
                red_x = l->x;
                assert(strcmp(l->text, expected[cursor]) == 0);
            }
        }
        assert(red_fields == 1u);
        assert(red_x != last_x);           /* 换字段必须换位置，屏幕要真的变 */
        last_x = red_x;

        /* 朱砂下划线：与该字段同 x、同宽，落在文字下方 2px。 */
        uint8_t underline = 0;
        for (uint8_t b = 0; b < v.box_n; ++b) {
            const xm_view_box_t *bx = &v.boxes[b];
            if (bx->color == XM_COLOR_RED && bx->h == 2u && bx->x == red_x) underline++;
        }
        assert(underline == 1u);
        assert(xm_layout_assert_footer(&v));
    }
    printf("  授时页 5 个字段各高亮一次、位置互不相同: PASS\n");
}

int main(void) {
    printf("test_xm_vm_layout\n");
    test_all_pages_pass_gate();
    test_hl_full_lines_pass_gate();
    test_ly_result_worst_case();
    test_keyguide_overlay();
    test_keyguide_lines_fit();
    test_overlay_banner_fits_status_zone();
    test_overlay_keeps_page_titles();
    test_time_cursor_highlight();
    printf("test_xm_vm_layout: PASS\n");
    return 0;
}
