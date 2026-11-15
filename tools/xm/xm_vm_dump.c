/*
 * xm_vm_dump.c —— 主机端（macOS / Linux）纯 C 视图转储器。
 *
 * 直接调用与固件完全相同的纯逻辑层（xm_pages_render / xm_vm_render），
 * 把每个页面的 xm_view_t 以 JSON 输出，供 gen_preview.py 渲染成 240×320 PNG。
 * 不依赖 ESP-IDF / LVGL / FreeRTOS：仅编译 core + generated + xm_pages.c。
 *
 * 天师白描位图以 "image" 字段导出名字与位置，gen_preview.py 按名字贴
 * assets/art/<name>.png；固件画的是同一份像素的 RGB565 常量（门禁逐像素校验）。
 *
 * 编译示例：
 *   cc -std=c11 -Wall -Wextra -Imain -Imain/core -Imain/generated \
 *      tools/xm/xm_vm_dump.c main/xm_pages.c main/core/xm_*.c \
 *      main/generated/xm_*_data.c -o /tmp/xm_vm_dump
 */
#include <stdio.h>
#include <string.h>

#include "xm_pages.h"
#include "xm_time.h"
#include "xm_store.h"
#include "xm_nav.h"
#include "xm_liuyao.h"
#include "xm_huangli_data.h"
#include "xm_vm.h"
#include "xm_tianshi_img.h"
#include "xm_yijing.h"

static const char *s_page_names[XM_PAGE_COUNT] = {
    "DISCLAIMER", "KEYGUIDE", "TIME", "HOME", "HL_A", "HL_B", "HL_C",
    "LY_CAST", "LY_RESULT", "LY_HISTORY", "SETTINGS", "ABOUT"
};

static xm_hl_table_t g_hl;

static void json_str(const char *s) {
    fputc('"', stdout);
    if (s) {
        for (const char *p = s; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
                case '"':  fputs("\\\"", stdout); break;
                case '\\': fputs("\\\\", stdout); break;
                case '\n': fputs("\\n", stdout); break;
                case '\r': fputs("\\r", stdout); break;
                case '\t': fputs("\\t", stdout); break;
                default:
                    if (c < 0x20) fprintf(stdout, "\\u%04x", c);
                    else fputc((int)c, stdout);
            }
        }
    }
    fputc('"', stdout);
}

static void json_stroke(const xm_view_stroke_t *s, int *printed) {
    if ((*printed)++) printf(", ");
    printf("{\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"color\":%u,\"width\":%u}",
           (int)s->x1, (int)s->y1, (int)s->x2, (int)s->y2,
           (unsigned)s->color, (unsigned)s->width);
}

static void dump_view(xm_page_t page, xm_nav_t *nav, xm_time_t *time,
                      xm_store_t *store, xm_liuyao_t *ly,
                      const uint8_t toss[6], uint8_t err_flags,
                      const char *label) {
    xm_view_t view = xm_pages_render(nav, time, store, ly, toss,
                                     &g_hl, 0, 87, err_flags);
    const char *ov = xm_vm_overlay_text(view.overlay);
    printf("  {\n");
    printf("    \"page\": %d,\n", (int)page);
    printf("    \"name\": ");
    json_str(label ? label
                   : ((page >= 0 && page < XM_PAGE_COUNT) ? s_page_names[page] : "UNKNOWN"));
    printf(",\n");
    printf("    \"overlay\": ");
    json_str(ov);
    printf(",\n");

    printf("    \"lines\": [");
    for (int i = 0; i < view.line_n; ++i) {
        xm_view_line_t *l = &view.lines[i];
        if (i) printf(", ");
        printf("{\"x\":%d,\"y\":%d,\"w\":%d,\"size\":%u,\"color\":%u,\"align\":%u,\"text\":",
               (int)l->x, (int)l->y, (int)l->w, (unsigned)l->size,
               (unsigned)l->color, (unsigned)l->align);
        json_str(l->text);
        printf("}");
    }
    printf("],\n");

    printf("    \"boxes\": [");
    for (int i = 0; i < view.box_n; ++i) {
        xm_view_box_t *b = &view.boxes[i];
        if (i) printf(", ");
        printf("{\"x\":%d,\"y\":%d,\"w\":%u,\"h\":%u,\"color\":%u,\"fill\":%d,\"border\":%d,\"radius\":%u}",
               (int)b->x, (int)b->y, (unsigned)b->w, (unsigned)b->h,
               (unsigned)b->color, b->fill ? 1 : 0, b->border ? 1 : 0,
               (unsigned)b->radius);
    }
    printf("],\n");

    /* 白描底图：预览按名字贴 PNG，与固件 RGB565 常量同源。 */
    printf("    \"image\": ");
    const char *img_name = NULL;
    if (view.image == tianshi_main_rgb565) img_name = "tianshi_main";
    else if (view.image == tianshi_home_rgb565) img_name = "tianshi_home";
    if (img_name) {
        printf("{\"name\":");
        json_str(img_name);
        printf(",\"x\":%d,\"y\":%d,\"w\":%u,\"h\":%u},\n",
               (int)view.image_x, (int)view.image_y,
               (unsigned)view.image_w, (unsigned)view.image_h);
    } else {
        printf("null,\n");
    }

    printf("    \"strokes\": [");
    int printed = 0;
    for (int i = 0; i < view.stroke_n; ++i) {
        json_stroke(&view.strokes[i], &printed);
    }
    printf("]\n");
    printf("  }");
}

typedef struct {
    xm_page_t page;
    uint8_t sel;
    uint8_t sub;
    uint8_t cast_count;
    int review_mode;
    int notice;
    int confirm_clear;
    int use_toss;
    uint8_t toss[6];
    uint8_t err_flags;
    uint8_t time_cursor;
    uint8_t cast_phase;   /* 0 等待确定键 / 1 摇动中 */
    uint8_t cast_frame;   /* 0..7 */
    const char *label;    /* 同名页区分用；NULL 则用页面名 */
} scenario_t;

int main(void) {
    xm_status_t hl_init_st = xm_hl_init(&g_hl, xm_huangli_blob, xm_huangli_blob_size);
    fprintf(stderr, "[diag] hl_init=%d blob_size=%u\n", (int)hl_init_st,
            (unsigned)xm_huangli_blob_size);
    {
        xm_hl_day_t d;
        memset(&d, 0, sizeof(d));
        xm_date_t dt = {2026, 11, 13, 10, 30};
        xm_status_t lk = xm_hl_lookup(&g_hl, &dt, &d);
        fprintf(stderr, "[diag] hl_lookup(2026-11-13)=%d lunar_y=%s\n",
                (int)lk, d.lunar_y ? d.lunar_y : "(null)");
    }
    (void)xm_yijing_init();

    xm_time_t time;
    xm_time_init(&time);
    xm_store_t store;
    xm_store_init(&store);

    scenario_t sc[] = {
        { XM_PAGE_DISCLAIMER, 0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_KEYGUIDE,   0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_HOME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_HL_A,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_HL_B,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_HL_C,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_LY_CAST,    0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, "LY_CAST_START" },
        { XM_PAGE_LY_RESULT,  0, 0, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, NULL },
        /* 解卦九屏逐屏预览（同一卦例）：卦象/动爻/总论/事业/财富/爱情/健康/家宅/占断。 */
        { XM_PAGE_LY_RESULT,  0, 1, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_1_DONGYAO" },
        { XM_PAGE_LY_RESULT,  0, 2, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_2_ZONGLUN" },
        { XM_PAGE_LY_RESULT,  0, 3, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_3_SHIYE" },
        { XM_PAGE_LY_RESULT,  0, 4, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_4_CAIFU" },
        { XM_PAGE_LY_RESULT,  0, 5, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_5_AIQING" },
        { XM_PAGE_LY_RESULT,  0, 6, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_6_JIANKANG" },
        { XM_PAGE_LY_RESULT,  0, 7, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_7_JIAZHAI" },
        { XM_PAGE_LY_RESULT,  0, 8, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "JIEGUO_8_ZHANDUAN" },
        { XM_PAGE_LY_HISTORY, 0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_SETTINGS,   0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_ABOUT,      0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        /* 异常态对照：红条退到顶栏，不许压住页面标题。 */
        { XM_PAGE_HOME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x01u, 0u, 0u, 0u, NULL },
        { XM_PAGE_HL_A,       0, 0, 0, 0, 0, 0, 0, {0}, 0x02u, 0u, 0u, 0u, NULL },
        /* 授时页 5 段高亮：确定键逐段推进，屏上必须真的变。 */
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, NULL },
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 1u, 0u, 0u, NULL },
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 2u, 0u, 0u, NULL },
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 3u, 0u, 0u, NULL },
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 4u, 0u, 0u, NULL },
        /* 上/下键调数值的前后对照：两帧都从真实 xm_pages_render 导出，
         * 只有 nav.draft_date 不同（渲染循环里给 AFTER 覆盖成 +1 年等）。
         * 修复前这里两帧会一模一样（显示源错取系统时钟）。 */
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, "TIME_KEY_BEFORE" },
        { XM_PAGE_TIME,       0, 0, 0, 0, 0, 0, 0, {0}, 0x00u, 0u, 0u, 0u, "TIME_KEY_AFTER" },
        /* 摇卦动画连续 7 帧（第 3 爻正在摇），供 GIF / 分帧拼图使用。 */
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 0u, "LY_CAST_SHAKE0" },
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 1u, "LY_CAST_SHAKE1" },
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 2u, "LY_CAST_SHAKE2" },
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 3u, "LY_CAST_SHAKE3" },
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 4u, "LY_CAST_SHAKE4" },
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 5u, "LY_CAST_SHAKE5" },
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 1u, 6u, "LY_CAST_SHAKE6" },
        /* 落定态：三爻成，提示「按确定键摇第 4 爻」；六爻齐：提示看卦象。 */
        { XM_PAGE_LY_CAST,    0, 0, 3, 0, 0, 0, 1, {7, 7, 8, 0, 0, 0}, 0x00u, 0u, 0u, 0u, "LY_CAST_SETTLED3" },
        { XM_PAGE_LY_CAST,    0, 0, 6, 0, 0, 0, 1, {7, 7, 8, 9, 8, 6}, 0x00u, 0u, 0u, 0u, "LY_CAST_READY" },
    };

    printf("[\n");
    int n = (int)(sizeof(sc) / sizeof(sc[0]));
    for (int i = 0; i < n; ++i) {
        scenario_t *p = &sc[i];
        xm_nav_t nav;
        xm_nav_init(&nav);
        nav.page = p->page;
        nav.sel = p->sel;
        nav.sub = p->sub;
        nav.cast_count = p->cast_count;
        nav.cast_phase = p->cast_phase;
        nav.cast_frame = p->cast_frame;
        nav.review_mode = (bool)p->review_mode;
        nav.notice = (bool)p->notice;
        nav.confirm_clear = (bool)p->confirm_clear;
        nav.time_cursor = (uint8_t)(p->time_cursor % 5u);
        /* AFTER 帧模拟「在年段按了一次上键 + 其它段被调过」后的草稿。 */
        if (p->label && strcmp(p->label, "TIME_KEY_AFTER") == 0) {
            nav.draft_date.year = 2027;
            nav.draft_date.month = 3;
            nav.draft_date.day = 7;
            nav.draft_date.hour = 9;
            nav.draft_date.minute = 45;
        }

        xm_liuyao_t ly;
        memset(&ly, 0, sizeof(ly));
        uint8_t toss[6] = {0};
        if (p->use_toss) {
            memcpy(toss, p->toss, 6);
            ly = xm_liuyao_from(toss);
        }
        if (i) printf(",\n");
        dump_view(p->page, &nav, &time, &store, &ly, toss, p->err_flags, p->label);
    }
    printf("\n]\n");
    return 0;
}
