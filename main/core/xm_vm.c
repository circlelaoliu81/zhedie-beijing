#include "xm_vm.h"

#include <assert.h>
#include <string.h>
#include "xm_build_id.h"
#include "xm_fontcov.h"
#include "xm_layout.h"
#include "xm_strings.h"
#include "xm_tianshi_img.h"
#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

#if defined(ESP_PLATFORM)
static const char *TAG = "xm_vm";
#endif

/* 长文本折行缓冲：只统计行数不够用（设备端 label 固定宽高，超出部分直接看不到），
 * 必须在 VM 内部把长句真正切成多段再逐段上屏。单次渲染独占，与 xm_pages.c 的 s_state 同策略。 */
#define XM_VM_WRAP_ROWS 16u
#define XM_VM_WRAP_COLS 96u
/* 六爻结果页正文行预算：字号 16px、行距 19、y=52 起 8 行到 185，
 * 底边 201 不压 y=226 起的两行来源说明与 y=266 页脚键位条。
 * 一屏内容拆成九屏（卦象/动爻/总论/事业/财富/爱情/健康/家宅/占断）后，
 * 每屏逻辑段落 + 折行后视觉行 ≤ 8，单屏即可完整显示，上/下键切屏看完全部解读。 */
#define XM_VM_LY_BODY_ROWS 8u
/* 注意：xm_view_t 只存 const char*，折行结果都指向这块静态缓冲，
 * 因此同一个 xm_view_t 必须在被绘制完之前保持有效。当前 render→xm_ui_apply_locked
 * 在同一个输入任务里连续完成，LVGL 任务不会插进来复用缓冲；
 * 千万不要把 LVGL 任务的优先级调到输入任务之上，否则会出现「上一帧的图，下一帧的字」。 */
static char s_wrap_rows[XM_VM_WRAP_ROWS][XM_VM_WRAP_COLS];

/* 首页只剩两项（黄历 / 六爻）。设备无摄像头、无罗盘，拿不到真实输入的两块功能
 * 已整体移除，不再作为菜单项出现，以免做成「假装能看」的页面。 */
static const char *const s_home_items[2] = {
    XM_HOME_ITEM_HL, XM_HOME_ITEM_LY
};
static const char *const s_setting_items[5] = {
    XM_SET_TIME, XM_SET_REVIEW, XM_SET_CLEAR, XM_SET_ABOUT, XM_SET_FONT
};

static void xm_vm_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0 || !src) return;
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

/* 字符步进与 xm_layout_wrap 的度量保持一致：CJK 宽 = size，ASCII 宽 = size/2 + 1。 */
static uint16_t xm_vm_advance(uint32_t cp, uint8_t size) {
    return cp < 0x80u ? (uint16_t)((size / 2u) + 1u) : (uint16_t)size;
}

static void xm_vm_wrap_reset(void) {
    memset(s_wrap_rows, 0, sizeof(s_wrap_rows));
}

/* 行首禁则：这些句读/收尾标点不允许落在行首（否则出现「。无攸利。」独占一行的破句）。 */
static bool xm_vm_no_start_punct(uint32_t cp) {
    switch (cp) {
        case 0x3002u: /* 。 */
        case 0x3001u: /* 、 */
        case 0xFF0Cu: /* ， */
        case 0xFF1Bu: /* ； */
        case 0xFF1Au: /* ： */
        case 0xFF1Fu: /* ？ */
        case 0xFF01u: /* ！ */
        case 0xFF09u: /* ） */
        case 0x300Bu: /* 》 */
        case 0x300Du: /* 》 */
        case 0x300Fu: /* 》 */
        case 0x201Du: /* ” */
        case 0x2026u: /* … */
        case 0xFF05u: /* ％ */
            return true;
        default:
            return false;
    }
}

/* 末行截断标记：回退最后一个 UTF-8 字符后补「…」，不越出缓冲容量。 */
static void xm_vm_wrap_ellipsis(uint8_t row, size_t used) {
    size_t cut = used;
    if (cut > 0u) {
        cut--;
        while (cut > 0u && ((uint8_t)s_wrap_rows[row][cut] & 0xC0u) == 0x80u) --cut;
    }
    static const char *const s_ellipsis = "…";
    const size_t ell = strlen(s_ellipsis);
    if (cut + ell < XM_VM_WRAP_COLS) {
        memcpy(&s_wrap_rows[row][cut], s_ellipsis, ell);
        s_wrap_rows[row][cut + ell] = '\0';
    } else {
        s_wrap_rows[row][cut] = '\0';
    }
}

/* 把 text 按像素宽度折行写入 s_wrap_rows[from .. max_rows)，返回实际占用行数。
 * 行数不够时最后一行截断并补「…」，*complete = false，调用方可据此停手。 */
static uint8_t xm_vm_wrap_into(const char *text, uint8_t size, uint16_t width_px,
                               uint8_t from, uint8_t max_rows, bool *complete) {
    if (complete) *complete = true;
    if (!text || text[0] == '\0' || size == 0u || width_px == 0u) return 0u;
    if (from >= max_rows || from >= XM_VM_WRAP_ROWS) return 0u;
    uint8_t row = from;
    uint16_t x = 0;
    size_t used = 0;
    size_t last_start = 0;
    uint16_t last_adv = 0;
    s_wrap_rows[row][0] = '\0';
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *start = cursor;
        const uint32_t cp = xm_utf8_decode(&cursor);
        if (cp == 0u || cp == 0xFFFDu) break;
        const size_t charm = (size_t)(cursor - start);
        const uint16_t adv = xm_vm_advance(cp, size);
        if (used > 0u && (uint16_t)(x + adv) > width_px) {
            char carry[4] = {0};
            size_t carry_n = 0u;
            uint16_t carry_adv = 0u;
            /* 行首禁则：把上一行末字一起下移，避免句读独占行首。 */
            if (xm_vm_no_start_punct(cp) && last_start > 0u) {
                carry_n = used - last_start;
                if (carry_n <= sizeof(carry)) {
                    memcpy(carry, &s_wrap_rows[row][last_start], carry_n);
                    carry_adv = last_adv;
                    s_wrap_rows[row][last_start] = '\0';
                } else {
                    carry_n = 0u;
                }
            }
            if ((uint8_t)(row + 1u) >= max_rows) {
                if (carry_n > 0u) { /* 没有新行可用，还原后再截断 */
                    memcpy(&s_wrap_rows[row][last_start], carry, carry_n);
                    s_wrap_rows[row][used] = '\0';
                }
                xm_vm_wrap_ellipsis(row, used);
                if (complete) *complete = false;
                break;
            }
            ++row;
            x = 0;
            used = 0;
            s_wrap_rows[row][0] = '\0';
            if (carry_n > 0u) {
                memcpy(&s_wrap_rows[row][0], carry, carry_n);
                used = carry_n;
                s_wrap_rows[row][used] = '\0';
                x = carry_adv;
                last_start = 0;
                last_adv = carry_adv;
            }
        }
        if (used + charm + 1u > XM_VM_WRAP_COLS) {
            xm_vm_wrap_ellipsis(row, used);
            if (complete) *complete = false;
            break;
        }
        last_start = used;
        last_adv = adv;
        memcpy(&s_wrap_rows[row][used], start, charm);
        used += charm;
        s_wrap_rows[row][used] = '\0';
        x = (uint16_t)(x + adv);
    }
    return (uint8_t)(row - from + 1u);
}

static void xm_vm_add_line(xm_view_t *view, int16_t x, int16_t y, uint16_t w,
                           uint8_t size, uint32_t color, uint8_t align,
                           const char *text) {
    if (!view || !text || view->line_n >= 24u) return;
    xm_view_line_t *line = &view->lines[view->line_n++];
    line->x = x;
    line->y = y;
    line->w = w;
    line->size = size;
    line->color = color;
    line->align = align;
    line->text = text;
}

static void xm_vm_add_box(xm_view_t *view, int16_t x, int16_t y, uint16_t w,
                          uint16_t h, uint32_t color, bool fill, bool border) {
    if (!view || view->box_n >= 32u) return;
    xm_view_box_t *box = &view->boxes[view->box_n++];
    box->x = x;
    box->y = y;
    box->w = w;
    box->h = h;
    box->color = color;
    box->fill = fill;
    box->border = border;
}

static void xm_vm_add_stroke(xm_view_t *view, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2, uint32_t color, uint8_t width) {
    if (!view || view->stroke_n >= 16u) return;
    xm_view_stroke_t *stroke = &view->strokes[view->stroke_n++];
    stroke->x1 = x1;
    stroke->y1 = y1;
    stroke->x2 = x2;
    stroke->y2 = y2;
    stroke->color = color;
    stroke->width = width;
}

/* 方孔铜钱：外圆（radius=7）+ 内方孔。摇卦动画帧里三枚一起抖动，
 * 用两个 box 表达，不引入位图资源；preview 与真机按同一 radius 绘制。 */
static void xm_vm_add_coin(xm_view_t *view, int16_t x, int16_t y) {
    if (!view || view->box_n + 2u > 32u) return;
    xm_view_box_t *outer = &view->boxes[view->box_n++];
    outer->x = x;
    outer->y = y;
    outer->w = 14;
    outer->h = 14;
    outer->color = XM_COLOR_RED;
    outer->fill = false;
    outer->border = true;
    outer->radius = 7u;
    xm_view_box_t *hole = &view->boxes[view->box_n++];
    hole->x = (int16_t)(x + 5);
    hole->y = (int16_t)(y + 5);
    hole->w = 4;
    hole->h = 4;
    hole->color = XM_COLOR_INK;
    hole->fill = true;
    hole->border = false;
    hole->radius = 0u;
}

/* 有效异常位：引导页只抑制 E1（首启第一屏用户必然尚未授时），E2/E3/E5 照旧上屏。 */
static uint8_t xm_vm_effective_flags(xm_page_t page, uint8_t err_flags) {
    if (page == XM_PAGE_KEYGUIDE) {
        return (uint8_t)(err_flags & (uint8_t)~XM_ERR_TIME_UNCALIBRATED_BIT);
    }
    return err_flags;
}

/* 页脚键位提示必须与本页状态机真正消费的按键一一对应。
 * 旧版 12 页统一写「上键下键选择 · 确定键进入」，在授时页（上下键调数值、
 * 确定键逐段推进）与摇卦页（只有确定键）同屏给出两套互相矛盾的口径，
 * 真机表现就是「键位不对 / 不知道该按哪个键」。
 * 注意：xm_layout_assert_footer 要求每页按键区都有一行真实提示（has_keys），
 * 所以这里不能留空，只能换成与该页状态机一致的口径。 */
static const char *xm_vm_key_hint(xm_page_t page, const xm_page_state_t *state) {
    switch (page) {
        case XM_PAGE_DISCLAIMER:
        case XM_PAGE_KEYGUIDE:
            /* 页内已有「按确定键确认」按钮 / 三键说明；页脚再写同一句会
             * 在 16px 下变成上下两行一模一样的大字。换成等效但不同措辞的
             * 键位口径，仍然只认确定键，不与按钮打架。 */
            return XM_KEY_NEXT;
        case XM_PAGE_TIME:
            return (state->time_cursor >= 4u) ? XM_TIME_LAST : XM_TIME_HINT;
        case XM_PAGE_LY_CAST:
            return XM_CAST_HINT;         /* 每按一次确定键摇一爻 */
        case XM_PAGE_LY_RESULT:
            return XM_RESULT_HISTORY;   /* 下键看历史、长按确定键回首页 */
        default:
            return XM_KEY_HINT;          /* 列表页：上/下键选择、确定键进入 */
    }
}

static void xm_vm_common(xm_view_t *view, uint8_t flags, const char *key_hint) {
    /* 顶栏状态区 0..22：有异常时这一行改由 xm_status.c 画朱砂状态条（x=8..184，
     * 与右侧电量 184..232 并排），因此不再画「玄门」字样。
     * 页面大标题（y=30~34 等）一个像素都没动。 */
    if (flags == 0u) {
        xm_vm_add_line(view, 8, 4, 150, XM_FONT_AUX, XM_COLOR_INK, XM_ALIGN_LEFT, XM_APP_TITLE);
    }
    if (key_hint) {
        /* 16px 键位提示必须落在 y=266：几何门禁要求按键区 [264,288) 内
         * y + size + 4 <= 288，266+20=286 刚好；仍写 270 会越界 2px。 */
        xm_vm_add_line(view, 8, 266, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                       XM_ALIGN_CENTER, key_hint);
    }
    xm_vm_add_line(view, 8, 298, 224, XM_FONT_BODY, XM_COLOR_RED,
                   XM_ALIGN_CENTER, XM_DISCLAIMER_FOOTER);
    xm_vm_add_box(view, 8, 22, 224, 1, XM_COLOR_LINE, true, false);
    xm_vm_add_box(view, 8, 288, 224, 1, XM_COLOR_RED, true, false);
    view->footer = true;
    view->overlay = flags;
}

/* 六爻结果页不再画右上角卦象图：正文占满整宽（y=52 起 8 行），卦象信息由
 * 子屏标题「六爻·卦象」与「本卦/之卦」文字、动爻屏的爻辞共同表达，屏幕小屏
 * 优先保证解读文字可读，不牺牲正文空间给装饰图。 */

/* 六爻摇卦页：6 行爻位（上爻在最上、初爻在最下）。
 * cast_phase=1 时 cast_count 指向「正在摇」的那一爻：该行画三枚抖动的方孔铜钱，
 * 不提前露出结果；动画落定（phase 回 0）后才画实爻 / 断爻。
 * 每行都有「已定 / 摇动中 / 待摇」三种可见状态，配合下方朱砂框里的
 * 「按确定键摇第 N 爻」，用户不会不知道按什么键到下一爻。 */
static void xm_vm_add_cast_lines(xm_view_t *view, const xm_page_state_t *state) {
    static const int8_t s_shake_dx[8] = {-7, 5, -4, 7, -5, 3, -2, 0};
    static const int8_t s_shake_dy[8] = {3, -4, 5, -3, 4, -5, 2, 0};
    const uint8_t frame = state->cast_frame > 7u ? 7u : state->cast_frame;
    const uint8_t shaking_pos = (state->cast_phase == 1u) ? state->cast_count : 0u;
    for (uint8_t row = 0; row < 6u; ++row) {
        const uint8_t pos = (uint8_t)(6u - row);
        const int16_t y = (int16_t)(70 + row * 16);
        const bool shaking = (state->cast_phase == 1u) && (pos == shaking_pos);
        const bool done = (state->cast_phase == 1u)
                              ? (pos < shaking_pos)
                              : (pos <= state->cast_count);
        if (shaking) {
            /* 当前行：朱砂游标 + 三枚方孔铜钱，随帧左右上下抖动。 */
            xm_vm_add_box(view, 46, (int16_t)(y - 3), 6, 6, XM_COLOR_RED, true, false);
            for (uint8_t c = 0u; c < 3u; ++c) {
                const uint8_t phase = (uint8_t)(frame + c * 2u);
                const int16_t dx = s_shake_dx[(uint8_t)(phase & 7u)];
                const int16_t dy = s_shake_dy[(uint8_t)((phase + 3u) & 7u)];
                const int16_t cx = (int16_t)(76 + c * 38 + dx);
                const int16_t cy = (int16_t)(y - 7 + dy);
                xm_vm_add_coin(view, cx, cy);
            }
            /* 速度线：把「正在摇」画成动作，而不是一句静态的「摇卦中…」。 */
            xm_vm_add_stroke(view, 60, (int16_t)(y - 10), 72, (int16_t)(y - 10),
                             XM_COLOR_SUBINK, 1);
            xm_vm_add_stroke(view, 168, (int16_t)(y + 10), 180, (int16_t)(y + 10),
                             XM_COLOR_SUBINK, 1);
        } else if (done) {
            const uint8_t toss = state->cast_toss[pos - 1u];
            const uint32_t color = (toss == 6u || toss == 9u) ? XM_COLOR_RED : XM_COLOR_INK;
            xm_vm_add_box(view, 46, (int16_t)(y - 2), 4, 4, color, true, false);
            if (toss == 7u || toss == 9u) {
                xm_vm_add_stroke(view, 60, y, 180, y, color, 3);
            } else {
                xm_vm_add_stroke(view, 60, y, 112, y, color, 3);
                xm_vm_add_stroke(view, 128, y, 180, y, color, 3);
            }
            if (toss == 6u || toss == 9u) {
                xm_vm_add_box(view, 186, (int16_t)(y - 3), 6, 6, XM_COLOR_RED, true, false);
            }
        } else {
            /* 未摇：浅赭占位短线，六行结构一眼可见。 */
            xm_vm_add_stroke(view, 60, y, 180, y, XM_COLOR_LINE, 2);
            xm_vm_add_box(view, 46, (int16_t)(y - 2), 4, 4, XM_COLOR_LINE, true, false);
        }
    }
}

void xm_vm_state_init(xm_page_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->page = XM_PAGE_HOME;
    state->datetime.date.year = 2026;
    state->datetime.date.month = 11;
    state->datetime.date.day = 13;
    state->datetime.date.hour = 10;
    state->datetime.date.minute = 30;
    xm_vm_copy(state->date_text, sizeof(state->date_text), "2026-11-13");
    xm_vm_copy(state->time_text, sizeof(state->time_text), "10:30");
    xm_vm_copy(state->today_line, sizeof(state->today_line), "今日干支 丙午 己亥 辛卯");
    xm_vm_copy(state->cast_text, sizeof(state->cast_text), "第 1 / 6 爻");
    xm_vm_copy(state->cast_prompt, sizeof(state->cast_prompt),
               XM_CAST_NEXT_PREFIX "1" XM_CAST_NEXT_SUFFIX);
    xm_vm_copy(state->time_progress, sizeof(state->time_progress), "第 1 / 5 段");
    xm_vm_copy(state->version, sizeof(state->version), XM_APP_VERSION);
    xm_vm_copy(state->build_id, sizeof(state->build_id), XM_BUILD_ID);
    /* 授时页默认字段文本（与上面的 date_text/time_text 同源）。 */
    xm_vm_copy(state->time_field_text[0], sizeof(state->time_field_text[0]), "2026");
    xm_vm_copy(state->time_field_text[1], sizeof(state->time_field_text[1]), "11");
    xm_vm_copy(state->time_field_text[2], sizeof(state->time_field_text[2]), "13");
    xm_vm_copy(state->time_field_text[3], sizeof(state->time_field_text[3]), "10");
    xm_vm_copy(state->time_field_text[4], sizeof(state->time_field_text[4]), "30");
    state->time_cursor = 0;
    state->battery_soc = 87;
    state->hl_valid = true;
}

const char *xm_vm_overlay_text(uint8_t flags) {
    if (flags & XM_ERR_HL_EXPIRED_BIT) return XM_ERR_E2;
    if (flags & XM_ERR_NVS_DEGRADED_BIT) return XM_ERR_E3;
    if (flags & XM_ERR_CONTENT_MISSING_BIT) return XM_ERR_E5;
    if (flags & XM_ERR_TIME_UNCALIBRATED_BIT) return XM_ERR_E1;
    return "";
}

/* 纯 ASCII 文本的像素宽（与 xm_vm_advance 同口径：ASCII = size/2 + 1）。 */
static uint16_t xm_vm_ascii_width(const char *text, uint8_t size) {
    uint16_t w = 0;
    for (const char *p = text; p && *p; ++p) {
        w = (uint16_t)(w + xm_vm_advance((uint32_t)(unsigned char)*p, size));
    }
    return w;
}

/* 授时页的一行字段（年-月-日 或 时:分）：按字段分段绘制并整体居中，
 * 当前编辑字段用朱砂色 + 下方 2px 短横，其余字段墨色。整行 y 坐标不变。
 * 早先这一行整体染红、5 个字段无从区分，双击确定键切字段时屏幕上毫无变化。 */
static void xm_vm_add_time_row(xm_view_t *view, int16_t y, uint8_t size, uint8_t cursor,
                               uint8_t base, const char *const *fields, uint8_t field_n,
                               const char *sep) {
    uint16_t w[3] = {0u, 0u, 0u};
    const uint16_t sep_w = xm_vm_ascii_width(sep, size);
    uint16_t total = 0;
    for (uint8_t i = 0; i < field_n; ++i) {
        w[i] = xm_vm_ascii_width(fields[i], size);
        total = (uint16_t)(total + w[i] + (i + 1u < field_n ? sep_w : 0u));
    }
    int16_t x = (int16_t)(8 + (int16_t)((224u - total) / 2u));
    for (uint8_t i = 0; i < field_n; ++i) {
        const bool active = (cursor == (uint8_t)(base + i));
        /* 段宽多给 4px：标签宽度按字符 advance 累加时，末字若贴边会被 LVGL 裁一点。 */
        xm_vm_add_line(view, x, y, (uint16_t)(w[i] + 4u), size,
                       active ? XM_COLOR_RED : XM_COLOR_INK, XM_ALIGN_LEFT, fields[i]);
        if (active) {
            /* 下划线补齐「颜色之外的第二重指示」，色弱用户也看得出在调哪一段。 */
            /* 下划线 y = 段基线 + 28（84→112、118→146）：压住下方空隙，
             * 又不碰提示行（y=160）。 */
            xm_vm_add_box(view, x, (int16_t)(y + 28), w[i], 2, XM_COLOR_RED, true, false);
        }
        if (i + 1u < field_n) {
            xm_vm_add_line(view, (int16_t)(x + w[i]), y, (uint16_t)(sep_w + 4u), size,
                           XM_COLOR_INK, XM_ALIGN_LEFT, sep);
            x = (int16_t)(x + w[i] + sep_w);
        }
    }
}

xm_view_t xm_vm_render(xm_page_t page, const xm_page_state_t *state) {
    xm_view_t view;
    memset(&view, 0, sizeof(view));
    /* 必须 static：xm_page_state_t 约 7.1KB，放在栈上时编译器给本函数分配
     * 7280B 栈帧，而 ESP32-C3 的 main 任务栈默认仅 3584B —— 真机首屏渲染
     * 必现 Stack protection fault 并无限重启（host 栈 8MB 完全测不出来）。
     * 该值仅在 state==NULL 时兜底使用，静态只读语义等价。 */
    static const xm_page_state_t fallback = {0};
    if (!state) state = &fallback;
    xm_vm_common(&view, xm_vm_effective_flags(page, state->err_flags),
                 xm_vm_key_hint(page, state));
    switch (page) {
        case XM_PAGE_DISCLAIMER: {
            /* 进入界面主视觉：传统白描天师半身像（莲花冠、剑眉凤眼、垂胸长髯、
             * 交领道袍、拂尘、符箓）。早期用 135 条直线段拼简笔画，真机被判
             * 「太过于抽象十分难看」，已废弃；现在是 RGB565 位图，与 host 预览同源。
             * y=26..158 夹在顶栏分割线(22)与大标题(164)之间，不压任何文字。 */
            view.image = tianshi_main_rgb565;
            view.image_x = 20;
            view.image_y = 24;
            view.image_w = TIANSHI_MAIN_W;
            view.image_h = TIANSHI_MAIN_H;
            xm_vm_add_line(&view, 8, 156, 224, 20, XM_COLOR_INK, XM_ALIGN_CENTER, XM_APP_TITLE);
            /* 标题两侧细线，像匾额，不再是一片空白加几行字。 */
            xm_vm_add_stroke(&view, 12, 168, 62, 168, XM_COLOR_LINE, 1);
            xm_vm_add_stroke(&view, 178, 168, 228, 168, XM_COLOR_LINE, 1);
            if (state->review_mode) {
                xm_vm_add_line(&view, 12, 190, 216, XM_FONT_BODY, XM_COLOR_INK,
                               XM_ALIGN_CENTER, XM_REVIEW_HINT);
            } else {
                /* 三行免责正文全部 16px，每行 <=13 个全角字（216px 宽）。
                 * 行距 19、y=180..218，底边 238，正好给 y=240 的按钮让位。 */
                xm_vm_add_line(&view, 12, 180, 216, XM_FONT_BODY, XM_COLOR_INK,
                               XM_ALIGN_CENTER, XM_DISCLAIMER_L1);
                xm_vm_add_line(&view, 12, 199, 216, XM_FONT_BODY, XM_COLOR_INK,
                               XM_ALIGN_CENTER, XM_DISCLAIMER_L2);
                xm_vm_add_line(&view, 12, 218, 216, XM_FONT_BODY, XM_COLOR_INK,
                               XM_ALIGN_CENTER, XM_DISCLAIMER_L3);
            }
            xm_vm_add_box(&view, 52, 240, 136, 24, XM_COLOR_RED, false, true);
            xm_vm_add_line(&view, 52, 244, 136, XM_FONT_BODY, XM_COLOR_RED,
                           XM_ALIGN_CENTER, XM_OK_ACK);
            break;
        }
        case XM_PAGE_KEYGUIDE:
            /* 三键操作引导（首启一次）。只写功能名，不写任何物理位置断言：
             * 设备三个功能键无刻字，用户只能靠「按一下试试 + 屏上中文功能名」对应。 */
            xm_vm_add_line(&view, 8, 44, 224, 20, XM_COLOR_INK, XM_ALIGN_CENTER, XM_GUIDE_TITLE);
            xm_vm_add_box(&view, 12, 88, 216, 112, XM_COLOR_LINE, false, true);
            xm_vm_add_line(&view, 24, 98, 192, 16, XM_COLOR_INK, XM_ALIGN_LEFT, XM_GUIDE_UP);
            xm_vm_add_line(&view, 24, 130, 192, 16, XM_COLOR_INK, XM_ALIGN_LEFT, XM_GUIDE_DOWN);
            xm_vm_add_line(&view, 24, 162, 192, 16, XM_COLOR_INK, XM_ALIGN_LEFT, XM_GUIDE_OK);
            /* 上/下键没有长按映射，返回只能长按确定键——必须写明按哪个键。 */
            xm_vm_add_line(&view, 8, 208, 224, 16, XM_COLOR_RED, XM_ALIGN_CENTER, XM_GUIDE_BACK);
            xm_vm_add_line(&view, 8, 236, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, XM_GUIDE_POWER);
            /* 本页抑制 E1 由 xm_vm_effective_flags() 在渲染入口统一处理，
             * 这里不再改写 view.overlay，避免与顶栏状态条判断出现分叉。 */
            break;
        case XM_PAGE_TIME: {
            xm_vm_add_line(&view, 8, 48, 224, 20, XM_COLOR_INK, XM_ALIGN_CENTER, XM_TIME_TITLE);
            /* 日期/时间按字段分段绘制：当前编辑字段朱砂色 + 下划线，其余墨色。
             * 字段 0..4 = 年/月/日/时/分，与 xm_nav.c 的 time_cursor 对齐。 */
            /* cursor 越界保护：>4 一律按 4 处理，脏数据也不许画到屏幕外。 */
            const uint8_t cursor = (state->time_cursor > 4u) ? 4u : state->time_cursor;
            const char *const date_fields[3] = {
                state->time_field_text[0], state->time_field_text[1], state->time_field_text[2]
            };
            const char *const time_fields[2] = {
                state->time_field_text[3], state->time_field_text[4]
            };
            xm_vm_add_time_row(&view, 84, 24, cursor, 0u, date_fields, 3u, "-");
            xm_vm_add_time_row(&view, 118, 24, cursor, 3u, time_fields, 2u, ":");
            /* 键位口径由页脚键位条（xm_vm_key_hint → XM_TIME_HINT / XM_TIME_LAST）
             * 统一给出，页内不再重复写同一句：字号从 12 提到 16 后，同屏两遍
             * 一模一样的大字非常刺眼。这里只把「第 N / 5 段」放大到 20px，
             * 让用户一眼看到自己正在调哪一段。 */
            xm_vm_add_line(&view, 8, 168, 224, 20, XM_COLOR_RED, XM_ALIGN_CENTER,
                           state->time_progress);
            break;
        }
        case XM_PAGE_HOME: {
            /* 首页用聚焦头肩的 160×94 位图：标题框 y=34..64、日期行 y=158..176，
             * 图像正好嵌在 y=64..158 的空档，两边都不压。 */
            view.image = tianshi_home_rgb565;
            view.image_x = 40;
            view.image_y = 64;
            view.image_w = TIANSHI_HOME_W;
            view.image_h = TIANSHI_HOME_H;
            xm_vm_add_line(&view, 8, 34, 224, 24, XM_COLOR_INK, XM_ALIGN_CENTER, XM_APP_TITLE);
            xm_vm_add_line(&view, 8, 154, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, state->today_line);
            /* 图下方两张菜单卡：卡高 32、间距 4，底边 244 不压 y=246 的长按提示。 */
            for (uint8_t i = 0; i < 2u; ++i) {
                const bool selected = (i == state->sel % 2u);
                const int16_t y = (int16_t)(176 + i * 36);
                xm_vm_add_box(&view, 16, y, 208, 32,
                              selected ? XM_COLOR_RED : XM_COLOR_LINE, false, true);
                if (selected) {
                    /* 选中项左侧朱砂竖条标记，与文字色一起构成双重指示。 */
                    xm_vm_add_box(&view, 8, (int16_t)(y + 8), 4, 16,
                                  XM_COLOR_RED, true, false);
                }
                xm_vm_add_line(&view, 28, (int16_t)(y + 6), 184, 16,
                               selected ? XM_COLOR_RED : XM_COLOR_INK,
                               XM_ALIGN_CENTER, s_home_items[i]);
            }
            xm_vm_add_line(&view, 8, 244, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, XM_HOME_HINT);
            break;
        }
        case XM_PAGE_HL_A:
        case XM_PAGE_HL_B:
        case XM_PAGE_HL_C: {
            const char *title = page == XM_PAGE_HL_A ? XM_HL_TITLE_A :
                                (page == XM_PAGE_HL_B ? XM_HL_TITLE_B : XM_HL_TITLE_C);
            xm_vm_add_line(&view, 8, 30, 224, 20, XM_COLOR_INK, XM_ALIGN_LEFT, title);
            /* 正文 12→16px 后 224px 全宽只容 14 个全角字，1096 天最坏 14 个文本行
             * 一屏根本放不下。改用两栏网格：表头 [0,hl_head_n) 全宽，
             * 宜/忌、吉神/凶煞、十二时辰按 hl_col_split 分左右栏（各 112px），
             * 14 个文本行压成 7~10 个视觉行，条目一条不丢、键位一个不改。 */
            const uint8_t n = state->hl_line_n > 14u ? 14u : state->hl_line_n;
            uint8_t head = state->hl_head_n;
            if (head > n) head = n;
            const uint8_t body_n = (uint8_t)(n - head);
            /* 两栏一律均分：按字段边界切分遇上「宜 8 行 / 忌 1 行」这类极端日子，
             * 长栏会一路排到 y>244 压进按键区（1096 天扫描实测会 assert）。
             * format_field 每行都自带「宜：/忌：」前缀，均分不会丢字段名。
             * 只有当字段边界本身接近均分、且不把视觉行数推过 10 行时才优先采用，
             * 让「宜」整段落左栏、「忌」整段落右栏，读起来更顺。 */
            uint8_t left_n = (uint8_t)((body_n + 1u) / 2u);
            const uint8_t split = state->hl_col_split;
            if (split >= head && split <= n) {
                const uint8_t boundary = (uint8_t)(split - head);
                const uint8_t right_at_boundary = (uint8_t)(body_n - boundary);
                const uint8_t worst = (uint8_t)(boundary > right_at_boundary
                                                ? boundary : right_at_boundary);
                if (head + worst <= 10u) left_n = boundary;
            }
            const uint8_t visual = (uint8_t)(head + left_n);
            const uint8_t step = (uint8_t)(visual > 10u ? 18u : 20u);
            /* 两栏之间一条淡赭竖线：左右各 112px、仅 8px 间隙，没有它时
             * 「宜：嫁娶 祭祀」和「忌：栽种 掘井」会连成一整行，读不出分栏。
             * 竖线只覆盖条目区，不穿过上方全宽表头文字。 */
            if (body_n > 0u && visual > head) {
                const int16_t y1 = (int16_t)(54 + (uint16_t)head * step);
                const int16_t y2 = (int16_t)(54 + ((uint16_t)visual - 1u) * step + 18);
                xm_vm_add_stroke(&view, 120, y1, 120, y2, XM_COLOR_LINE, 1);
            }
            for (uint8_t i = 0; i < n; ++i) {
                const bool right = (i >= (uint16_t)head + left_n);
                const uint8_t row = (uint8_t)(right ? (i - head - left_n) : (i - head));
                uint32_t color = XM_COLOR_INK;
                if (page == XM_PAGE_HL_C && i == state->hour_index) color = XM_COLOR_RED;
                if (i < head) {
                    xm_vm_add_line(&view, 8, (int16_t)(54 + i * step), 224,
                                   XM_FONT_BODY, color, XM_ALIGN_LEFT, state->hl_lines[i]);
                } else {
                    xm_vm_add_line(&view, right ? 124 : 4,
                                   (int16_t)(54 + ((uint16_t)head + row) * step),
                                   XM_PAGES_COL_W, XM_FONT_BODY, color,
                                   XM_ALIGN_LEFT, state->hl_lines[i]);
                }
            }
            if (page == XM_PAGE_HL_C) {
                /* 「当前」不再写进时辰行尾（会撑破 112px 栏宽），改由朱砂色 +
                 * 一行说明表达：信息不丢，且当前时辰一眼可见。 */
                xm_vm_add_line(&view, 8, 196, 224, XM_FONT_BODY, XM_COLOR_RED,
                               XM_ALIGN_CENTER, XM_HL_CURRENT);
                xm_vm_add_line(&view, 8, 232, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                               XM_ALIGN_CENTER, XM_SRC_JISHI);
            }
            break;
        }
        case XM_PAGE_LY_CAST:
            xm_vm_add_line(&view, 8, 32, 224, 24, XM_COLOR_INK, XM_ALIGN_CENTER, state->cast_text);
            xm_vm_add_cast_lines(&view, state);
            /* 朱砂框里的这句是「下一步按哪个键」的唯一答案：动画中写摇卦中，
             * 落定后写「按确定键摇第 N 爻」，六爻齐了写「按确定键看卦象」。 */
            xm_vm_add_box(&view, 12, 170, 216, 32, XM_COLOR_RED, false, true);
            xm_vm_add_line(&view, 12, 176, 216, 16, XM_COLOR_RED,
                           XM_ALIGN_CENTER, state->cast_prompt);
            /* 「每按一次确定键摇一爻」统一放页脚键位条（xm_vm_key_hint），
             * 这里不再重复画一遍：红框答「下一步」，页脚答「怎么按」，各司其职。 */
            xm_vm_add_line(&view, 8, 230, 224, XM_FONT_BODY,
                           state->notice ? XM_COLOR_RED : XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, XM_CAST_ABANDON);
            break;
        case XM_PAGE_LY_RESULT: {
            /* 结果页拆九屏（子屏序号 state->sub）：卦象 / 动爻 / 总论 / 事业 /
             * 财富 / 爱情 / 健康 / 家宅 / 占断。分维度解卦是用户真正要的内容。
             * 顶部 20px 朱砂子屏标题告知用户在第几屏；正文整宽 y=52 起 8 视觉行，
             * 每屏内容折行后 ≤ 8 行，单屏即完整；上/下键切屏、确定进历史、长按回首页。
             * 卦象信息由「本卦/之卦」文字 + 动爻屏爻辞表达，不画装饰图，正文空间最大化。 */
            static const char *const s_ly_title[9] = {
                "六爻·卦象", "六爻·动爻", "六爻·总论", "六爻·事业", "六爻·财富",
                "六爻·爱情", "六爻·健康", "六爻·家宅", "六爻·占断"
            };
            const uint8_t sub = state->sub < 9u ? state->sub : 8u;
            xm_vm_add_line(&view, 8, 30, 200, 20, XM_COLOR_RED, XM_ALIGN_LEFT,
                           s_ly_title[sub]);
            xm_vm_wrap_reset();
            uint8_t body_rows = 0;
            for (uint8_t i = 0u;
                 i < state->result_line_n && i < 8u && body_rows < XM_VM_LY_BODY_ROWS; ++i) {
                bool complete = true;
                const uint8_t got = xm_vm_wrap_into(state->result_lines[i], XM_FONT_BODY, 224,
                                                   body_rows, XM_VM_LY_BODY_ROWS, &complete);
                body_rows = (uint8_t)(body_rows + got);
                if (!complete) break;
            }
            for (uint8_t i = 0; i < body_rows && i < XM_VM_LY_BODY_ROWS; ++i) {
                xm_vm_add_line(&view, 8, (int16_t)(52 + i * 19), 224, XM_FONT_BODY,
                               XM_COLOR_INK, XM_ALIGN_LEFT, s_wrap_rows[i]);
            }
            /* 数据来源说明：古经原文与今人整理各据其位（16px，落在页脚键位条之上）。 */
            xm_vm_add_line(&view, 8, 222, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, XM_RESULT_CLASSIC);
            xm_vm_add_line(&view, 8, 242, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, XM_RESULT_SRC);
            break;
        }
        case XM_PAGE_LY_HISTORY:
            xm_vm_add_line(&view, 8, 32, 224, 20, XM_COLOR_INK, XM_ALIGN_CENTER, XM_HISTORY_TITLE);
            if (state->history_empty) {
                xm_vm_add_line(&view, 8, 100, 224, 20, XM_COLOR_SUBINK,
                               XM_ALIGN_CENTER, XM_HISTORY_EMPTY);
            } else {
                /* 16px 单行：日期时间 + 「本卦→变卦」紧凑连写（实测 217px <=224px）。
                 * 行距 22，8 条记录 y=56..210，底边 230，不压底部提示。 */
                for (uint8_t i = 0; i < state->history_count && i < 8u; ++i) {
                    xm_vm_add_line(&view, 8, (int16_t)(56 + i * 22), 224, XM_FONT_BODY,
                                   i == state->sel ? XM_COLOR_RED : XM_COLOR_INK,
                                   XM_ALIGN_LEFT, state->history_text[i]);
                }
            }
            xm_vm_add_line(&view, 8, 244, 224, XM_FONT_BODY, XM_COLOR_SUBINK,
                           XM_ALIGN_CENTER, XM_HISTORY_CLEAR);
            break;
        case XM_PAGE_SETTINGS:
            xm_vm_add_line(&view, 8, 30, 224, 20, XM_COLOR_INK, XM_ALIGN_CENTER, XM_SETTINGS_TITLE);
            for (uint8_t i = 0; i < 5; ++i) {
                const bool selected = (i == state->sel);
                const bool disabled = (i == 4);
                const int16_t y = (int16_t)(60 + i * 36);
                xm_vm_add_box(&view, 12, y, 216, 30, selected ? XM_COLOR_RED : XM_COLOR_LINE, false, true);
            xm_vm_add_line(&view, 20, (int16_t)(y + 6), 200, 16,
                               disabled ? XM_COLOR_DISABLE : (selected ? XM_COLOR_RED : XM_COLOR_INK),
                               XM_ALIGN_LEFT, s_setting_items[i]);
            }
            if (state->settings_confirm_clear) {
                xm_vm_add_line(&view, 8, 244, 224, XM_FONT_BODY, XM_COLOR_RED,
                               XM_ALIGN_CENTER, XM_CLEAR_CONFIRM);
            }
            break;
        case XM_PAGE_ABOUT:
            xm_vm_add_line(&view, 8, 30, 224, 20, XM_COLOR_INK, XM_ALIGN_CENTER, XM_ABOUT_TITLE);
            /* 10 行 16px 正文，行距 20，y=54..234，底边 254 不越按键区。 */
            for (uint8_t i = 0; i < state->about_line_n && i < 10u; ++i) {
                xm_vm_add_line(&view, 8, (int16_t)(54 + i * 20), 224, XM_FONT_BODY,
                               XM_COLOR_INK, XM_ALIGN_LEFT, state->about_lines[i]);
            }
            break;
        default:
            xm_vm_add_line(&view, 8, 100, 224, 16, XM_COLOR_RED, XM_ALIGN_CENTER, XM_ERR_E5);
            break;
    }
    /* 几何门禁：正文/方框/线段不得越入按键区（264）与页脚区（288）。
     * 以前这里写成 (void)… 把结果直接丢弃，等于门禁从来没生效过；现在接上真实告警：
     * 固件用 ESP_LOGW 留现场，host（xm_vm_dump / 预览）直接断言失败，防止再漏。 */
    if (!xm_layout_assert_footer(&view)) {
#if defined(ESP_PLATFORM)
        ESP_LOGW(TAG, "布局越界：page=%d lines=%u boxes=%u strokes=%u",
                 (int)page, (unsigned)view.line_n, (unsigned)view.box_n,
                 (unsigned)view.stroke_n);
#else
        assert(!"xm_vm: 布局越界（正文压入按键区或页脚区，见 xm_layout_assert_footer）");
#endif
    }
    return view;
}
