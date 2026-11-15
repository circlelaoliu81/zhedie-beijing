/* test_xm_hl_width.c —— 黄历三子页「逐行宽度」全量扫描测试。
 *
 * 背景：xm_pages_format_field() 早先只按「每行 6 条」分组，从不判宽度。
 * 宜/忌条目 2~5 字不等，6 条一拼就超出 224px 可用宽；而 main/xm_ui.c 里
 * 这些 label 是固定 224×18 且未开 long_mode，超宽部分会被静默折到第二行、
 * 再被固定行高裁掉 —— 用户看不到任何报错，只是少看了 1~2 个宜项。
 * 实测最坏 2026-10-18「宜：解除 祭祀 修饰垣墙 平治道涂 造畜稠 馀事勿取」= 265px。
 *
 * 本测试把 2026-01-01 ~ 2028-12-31 共 1096 天 × 3 页全跑一遍：
 *   1. 每一行正文的估算宽度必须 <= 该行的 w（不超宽被裁）；
 *   2. 每一页都必须仍然出现「宜：」「忌：」/「吉神：」「凶煞：」（不被挤没）；
 *   3. 行数不超过 VM 的行预算（门禁另有几何断言，这里只防溢出）。
 * 度量口径与设备一致：CJK 宽 = size，ASCII 宽 = size/2 + 1。
 *
 * ── 负向对照怎么做（QA 实测踩过的坑，务必照此执行）────────────────────
 * 本测试要证明「宽度预算真的在起作用」。推荐流程（形态 C，实测与 -I 顺序无关）：
 *
 *   rm -rf /tmp/mut2 && mkdir -p /tmp/mut2
 *   cp main/xm_pages.c main/core/xm_layout.h /tmp/mut2/
 *   sed -i '' 's/XM_LAYOUT_SAFE_W 224u/XM_LAYOUT_SAFE_W 9999u/' /tmp/mut2/xm_layout.h
 *   cc -std=c11 -Wall -Wextra -Werror -I/tmp/mut2 -Imain -Imain/core -Imain/generated \
 *      tests/test_xm_hl_width.c /tmp/mut2/xm_pages.c main/core/xm_*.c \
 *      main/generated/xm_*_data.c -o /tmp/mut2/t && /tmp/mut2/t
 *
 * 正确变异的结果：FAIL: 331 行超宽，最宽超出 63 px，断言 abort（exit 134）。
 *
 * ── 规则：引号包含的查找顺序 ──────────────────────────────────────────
 * `#include "xm_layout.h"` 的解析顺序是：
 *   (1) 被包含文件（.c）自身所在目录  ->  (2) -I 路径，按命令行先后  ->  (3) 系统路径
 * 所以「变异有没有生效」取决于头文件相对于 .c 的位置和 -I 顺序。四种形态实测：
 *
 *   形态 | .c 位置          | 变异头位置      | -I 顺序        | 预处理结果 | 变异
 *   -----|------------------|-----------------|----------------|-----------|------
 *    C   | /tmp/mut2/（副本） | /tmp/mut2/     | 任意           | 9999u     | 生效
 *    B1  | /tmp/mutA/（副本） | /tmp/mutA/core/ | 变异目录在前   | 9999u     | 生效
 *    B2  | /tmp/mutA/（副本） | /tmp/mutA/core/ | 仓库在前       | 224u      | 假绿
 *    D   | 仓库原版          | /tmp/mutdoc/   | 变异目录在前   | 9999u     | 生效
 *    D   | 仓库原版          | /tmp/mutdoc/   | 仓库在前       | 224u      | 假绿
 *
 * 注意 D 形态：main/xm_pages.c 与 main/core/xm_layout.h **不同目录**，所以 (1)
 * 落空，最终仍按 -I 顺序找 —— D 并非必然无效，和 B1 一样依赖 -I 顺序。
 * （若哪天把头文件挪到 .c 同目录，D 就会在 (1) 命中、永远无法变异。）
 *
 * **唯一与 -I 顺序无关的形态是 C**：变异头与被编译的 .c 副本同目录，命中 (1)，
 * 无论 -I 怎么排都恒展开。B1/D 也可能生效，但依赖 -I 顺序，很脆。
 *
 * ── 别靠猜：用 -E 直接看宏有没有展开 ──────────────────────────────────
 * 「测试没红」可能是逻辑没被破坏，但「宏没展开」就是百分之百没变异。用预处理
 * 自检比看测试结果更直接：
 *
 *   cc -std=c11 -E <你的编译参数> /tmp/mut2/xm_pages.c \
 *     | grep -m1 -A2 'xm_pages_format_field("宜'
 *
 *   看到 12u, 9999u           -> 变异生效
 *   看到 12u, XM_LAYOUT_SAFE_W -> 宏根本没被替换 -> 本次是假绿
 *   看到 12u, 224u            -> 命中了仓库原头文件 -> 本次是假绿
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_fontcov.h"
#include "xm_huangli_data.h"
#include "xm_liuyao.h"
#include "xm_nav.h"
#include "xm_pages.h"
#include "xm_store.h"
#include "xm_time.h"
#include "xm_vm.h"

#define HL_LINE_BUDGET 14u

static xm_hl_table_t g_hl;

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

static uint8_t count_prefix_in_view(const xm_view_t *v, const char *prefix) {
    uint8_t n = 0;
    const size_t pl = strlen(prefix);
    for (uint8_t i = 0; i < v->line_n; ++i) {
        if (strncmp(v->lines[i].text, prefix, pl) == 0) n++;
    }
    return n;
}

typedef struct {
    uint16_t worst_px;
    uint16_t worst_w;
    char worst_text[128];
    xm_page_t worst_page;
    int64_t worst_day;
    uint16_t overflow_n;
} sweep_stat_t;

static void sweep_day(const xm_date_t *date, int64_t day, sweep_stat_t *stat) {
    xm_time_t time;
    xm_time_init(&time);
    xm_time_set(&time, date, 0);

    xm_store_t store;
    xm_store_init(&store);
    xm_liuyao_t ly;
    memset(&ly, 0, sizeof(ly));
    uint8_t toss[6] = {0};

    static const xm_page_t pages[3] = {XM_PAGE_HL_A, XM_PAGE_HL_B, XM_PAGE_HL_C};
    for (uint8_t p = 0; p < 3u; ++p) {
        xm_nav_t nav;
        xm_nav_init(&nav);
        nav.page = pages[p];
        const xm_view_t view = xm_pages_render(&nav, &time, &store, &ly, toss,
                                               &g_hl, 0, 87, 0);

        /* 正文行必须仍在上限内：14 行 × 14px 从 y=54 起，底边 248 < 264。 */
        uint8_t body_lines = 0;
        for (uint8_t i = 0; i < view.line_n; ++i) {
            const xm_view_line_t *l = &view.lines[i];
            const uint16_t px = approx_width(l->text, l->size);
            if (px > l->w) {
                stat->overflow_n++;
                if (px - l->w > stat->worst_px) {
                    stat->worst_px = (uint16_t)(px - l->w);
                    stat->worst_w = l->w;
                    stat->worst_page = pages[p];
                    stat->worst_day = day;
                    (void)snprintf(stat->worst_text, sizeof(stat->worst_text), "%s", l->text);
                }
            }
            if (l->y >= 54 && l->y < 264) body_lines++;
        }
        assert(body_lines <= HL_LINE_BUDGET);

        /* 分区标签不许因为换行把行数吃光而整段消失。 */
        if (pages[p] == XM_PAGE_HL_A) {
            assert(count_prefix_in_view(&view, "宜：") >= 1u);
            assert(count_prefix_in_view(&view, "忌：") >= 1u);
        } else if (pages[p] == XM_PAGE_HL_B) {
            assert(count_prefix_in_view(&view, "吉神：") >= 1u);
            assert(count_prefix_in_view(&view, "凶煞：") >= 1u);
        } else {
            /* HL_C：十二时辰必须一行一个，共 12 行。 */
            uint8_t hours = 0;
            static const char *const names[12] = {
                "子 ", "丑 ", "寅 ", "卯 ", "辰 ", "巳 ",
                "午 ", "未 ", "申 ", "酉 ", "戌 ", "亥 "
            };
            for (uint8_t h = 0; h < 12u; ++h) {
                if (count_prefix_in_view(&view, names[h]) >= 1u) hours++;
            }
            assert(hours == 12u);
        }
    }
}

int main(void) {
    printf("test_xm_hl_width\n");
    assert(xm_hl_init(&g_hl, xm_huangli_blob, xm_huangli_blob_size) == XM_OK);

    const int64_t first = xm_days_from_civil(2026, 1, 1);
    const int64_t last = xm_days_from_civil(2028, 12, 31);
    sweep_stat_t stat;
    memset(&stat, 0, sizeof(stat));
    int days = 0;
    for (int64_t d = first; d <= last; ++d) {
        int year = 0;
        int month = 0;
        int day = 0;
        xm_civil_from_days(d, &year, &month, &day);
        const xm_date_t date = {
            (uint16_t)year, (uint8_t)month, (uint8_t)day, 10, 30
        };
        sweep_day(&date, d, &stat);
        days++;
    }

    if (stat.overflow_n != 0u) {
        printf("  FAIL: %u 行超宽，最宽超出 %u px（w=%u，page=%d，day=%lld）: %s\n",
               (unsigned)stat.overflow_n, (unsigned)stat.worst_px,
               (unsigned)stat.worst_w, (int)stat.worst_page,
               (long long)stat.worst_day, stat.worst_text);
    }
    assert(stat.overflow_n == 0u);
    printf("  %d 天 × 3 页逐行宽度扫描：0 行超宽: PASS\n", days);
    printf("test_xm_hl_width: PASS\n");
    return 0;
}
