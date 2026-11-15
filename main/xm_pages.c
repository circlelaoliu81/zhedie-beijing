#include "xm_pages.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "xm_build_id.h"
#include "xm_layout.h"
#include "xm_strings.h"
#include "xm_yijing.h"

static xm_page_state_t s_state;

static void xm_pages_copy(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0 || !src) return;
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static void xm_pages_fmt(char *buf, size_t cap, const char *fmt, ...) {
    if (!buf || cap == 0 || !fmt) return;
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, cap, fmt, args);
    va_end(args);
    (void)xm_utf8_terminate_safe(buf, cap);
}

static bool xm_pages_append(char *buf, size_t cap, const char *text) {
    if (!buf || cap == 0 || !text) return false;
    const size_t used = strlen(buf);
    const size_t add = strlen(text);
    if (used == 0) {
        if (add + 1 > cap) return false;
        memcpy(buf, text, add + 1);
        return true;
    }
    if (used + 1 + add + 1 > cap) return false;
    buf[used] = ' ';
    memcpy(buf + used + 1, text, add + 1);
    return true;
}

static bool xm_pages_concat(char *buf, size_t cap, const char *text) {
    if (!buf || cap == 0 || !text) return false;
    const size_t used = strlen(buf);
    const size_t add = strlen(text);
    if (used + add + 1 > cap) return false;
    memcpy(buf + used, text, add + 1);
    return true;
}

/* 黄历三页「宜：/忌：/吉神：/凶煞：」行的排版参数。
 * 字号与可用宽度由调用方传入（不写死在函数里），度量口径与 xm_layout.c 一致：
 * CJK 宽 = font_size、ASCII 宽 = font_size/2 + 1、前缀宽度一并计入。
 *
 * 正文字号 12→16：用户真机反馈「内容页字号太小，不好看清」。16px 下
 * 224px 全宽最多 14 个全角字，1096 天实测黄历 A 最坏要 14 个文本行，
 * 一屏（内容区 22..264）根本放不下。改用两栏网格：表头仍全宽，
 * 宜/忌、吉神/凶煞、十二时辰按 112px 栏宽分组并左右并排，
 * 14 个文本行压成 7~10 个视觉行，字大 33% 且条目一条不丢。 */
#define XM_PAGES_HL_FONT 16u
/* 栏宽常量 XM_PAGES_COL_W 定义在 xm_vm.h（VM 与 pages 必须同宽），此处不重复定义。 */
/* 这一行再加上 item 之后是否仍只需一行（按像素宽度算，不按条目数）。
 * 早先只按「每行 6 条」分组，条目 2~5 字不等，6 条一拼就超宽：
 * 2026-10-18「宜：解除 祭祀 修饰垣墙 平治道涂 造畜稠 馀事勿取」按设备度量 287px，
 * 超出部分被 18px 定高标签静默折到第二行再裁掉，用户少看到 1~2 个宜项。 */
static bool xm_pages_line_fits(const char *line, const char *item,
                               uint8_t font_size, uint16_t width_px) {
    char probe[128];
    xm_pages_copy(probe, sizeof(probe), line);
    if (!xm_pages_append(probe, sizeof(probe), item)) return false;
    return xm_layout_wrap(probe, font_size, width_px) <= 1u;
}

static void xm_pages_format_field(const char *prefix, const xm_field_t *field,
                                  char lines[][128], uint8_t max_lines,
                                  uint8_t *line_n, uint8_t items_per_line,
                                  uint8_t font_size, uint16_t width_px) {
    uint8_t in_line = 0;
    /* 行首状态只由本函数维护，不再靠「缓冲是否为空」判断：
     * 历史上 hl_lines[] 残留上一版内容时，前缀会被整个跳过，结果把
     * 「宜：嫁娶 祭祀…」拼到残留的时辰行后面，用户看到的是「辰 壬辰 · 凶 嫁娶…」。
     * 行首项直接紧贴前缀（不插空格），避免出现「宜： 嫁娶」这种多余空隙。 */
    bool at_line_start = true;
    for (uint8_t i = 0; i < field->count && *line_n < max_lines; ++i) {
        const char *item = field->items[i];
        char *buf = lines[*line_n];
        bool start_new = at_line_start;
        if (!start_new && !xm_pages_line_fits(buf, item, font_size, width_px)) {
            /* 本行已装不下这一条：换行重来，不让它溢出到 18px 定高标签之外。 */
            start_new = true;
        } else if (!start_new) {
            if (xm_pages_append(buf, 128, item)) in_line++;
            else start_new = true;
        }
        if (start_new) {
            if (!at_line_start) {
                if (*line_n + 1 >= max_lines) return;
                (*line_n)++;
            }
            buf = lines[*line_n];
            xm_pages_copy(buf, 128, prefix);
            (void)xm_pages_concat(buf, 128, item);
            in_line = 1;
            at_line_start = false;
        }
        if (in_line >= items_per_line) {
            (*line_n)++;
            in_line = 0;
            at_line_start = true;
        }
    }
    if (in_line > 0 && *line_n < max_lines) (*line_n)++;
}

/* 三张黄历子页各自全量重写 hl_lines[]，因此进入时先整块清零：
 * 否则同一帧里先算过别的子页，留下 12 行时辰文本，前缀判断与行数都会错。 */
static void xm_pages_hl_reset(void) {
    memset(s_state.hl_lines, 0, sizeof(s_state.hl_lines));
}

/* 黄历查不到数据时的正文文案：超出 2026-01-01~2028-12-31 就直说「已过期」，
 * 只有真正读不到数据才说「内容缺失」——早先两种情况都写 E5，会出现
 * 顶栏说「黄历已过期」、正文却说「内容缺失：数据未载入」的自相矛盾。 */
static const char *xm_pages_hl_missing_text(void) {
    return (s_state.err_flags & XM_ERR_HL_EXPIRED_BIT) ? XM_ERR_E2 : XM_ERR_E5;
}

static void xm_pages_fill_hl_a(void) {
    uint8_t n = 0;
    xm_pages_hl_reset();
    if (!s_state.hl_valid) {
        s_state.hl_head_n = 1u;
        s_state.hl_col_split = 1u;
        xm_pages_copy(s_state.hl_lines[n++], 128, xm_pages_hl_missing_text());
    } else {
        /* 公历与农历各自成行：合排实测 249px（"公历 2026-11-13 农历 二〇二六 十月 初五"），
         * 超出 224px 可用宽，末字「五」会被右边缘切掉。
         * 农历行在 16px 下原本 228px（闰月最坏），去掉月/日之间的空格压到 201px。 */
        xm_pages_fmt(s_state.hl_lines[n++], 128, "公历 %s", s_state.date_text);
        xm_pages_fmt(s_state.hl_lines[n++], 128, "农历 %s%s%s%s",
                     s_state.hl.lunar_y, s_state.hl.lunar_m,
                     s_state.hl.lunar_d, s_state.hl.lunar_leap ? "闰" : "");
        xm_pages_fmt(s_state.hl_lines[n++], 128, "干支 %s %s %s",
                     s_state.hl.gz_y, s_state.hl.gz_m, s_state.hl.gz_d);
        xm_pages_fmt(s_state.hl_lines[n++], 128, "值神 %s · 建除 %s",
                     s_state.hl.tianshen, s_state.hl.zhixing);
        xm_pages_fmt(s_state.hl_lines[n++], 128, "宿 %s %s · 纳音 %s",
                     s_state.hl.xiu, s_state.hl.xiu_luck ? "吉" : "凶", s_state.hl.nayin);
        /* 页内共 14 个文本行可用，两栏并排后只占 7 个视觉行。
         * 表头 5 行全宽，宜走左栏、忌走右栏，各按 112px 栏宽分组；
         * hl_col_split 记下「忌：」起始位置，VM 据此分栏。 */
        xm_pages_format_field("宜：", &s_state.hl.yi, s_state.hl_lines, 11, &n, 6,
                              XM_PAGES_HL_FONT, XM_PAGES_COL_W);
        s_state.hl_col_split = n;
        if (n < 14) xm_pages_format_field("忌：", &s_state.hl.ji, s_state.hl_lines, 14, &n, 6,
                              XM_PAGES_HL_FONT, XM_PAGES_COL_W);
        s_state.hl_head_n = 5u;
    }
    s_state.hl_line_n = n;
}

static void xm_pages_fill_hl_b(void) {
    uint8_t n = 0;
    xm_pages_hl_reset();
    if (!s_state.hl_valid) {
        s_state.hl_head_n = 1u;
        s_state.hl_col_split = 1u;
        xm_pages_copy(s_state.hl_lines[n++], 128, xm_pages_hl_missing_text());
    } else {
        xm_pages_fmt(s_state.hl_lines[n++], 128, "冲 %s · 煞 %s",
                     s_state.hl.chong, s_state.hl.sha);
        /* 16px 下「喜神 西北 · 福神 西南 · 财神 东北」实测 287px，全宽会被裁掉末字。
         * 拆成两行：喜神/福神一行、财神一行，表头由 5 行变 6 行。 */
        xm_pages_fmt(s_state.hl_lines[n++], 128, "喜神 %s · 福神 %s",
                     s_state.hl.pos_xi, s_state.hl.pos_fu);
        xm_pages_fmt(s_state.hl_lines[n++], 128, "财神 %s", s_state.hl.pos_cai);
        xm_pages_fmt(s_state.hl_lines[n++], 128, "日纳音 %s", s_state.hl.nayin);
        /* 彭祖百忌两句各自成行：合排要 231px，超出 224px 可用宽会把末字裁掉。 */
        xm_pages_fmt(s_state.hl_lines[n++], 128, "彭祖 %s", s_state.hl.pengzu_gan);
        xm_pages_fmt(s_state.hl_lines[n++], 128, "彭祖 %s", s_state.hl.pengzu_zhi);
        /* 吉神走左栏、凶煞走右栏，各按 112px 栏宽分组（16px 字号）。 */
        xm_pages_format_field("吉神：", &s_state.hl.jishen, s_state.hl_lines, 11, &n, 6,
                              XM_PAGES_HL_FONT, XM_PAGES_COL_W);
        s_state.hl_col_split = n;
        if (n < 14) xm_pages_format_field("凶煞：", &s_state.hl.xiongsha, s_state.hl_lines, 14, &n, 6,
                              XM_PAGES_HL_FONT, XM_PAGES_COL_W);
        s_state.hl_head_n = 6u;
    }
    s_state.hl_line_n = n;
}

static void xm_pages_fill_hl_c(void) {
    static const char *const s_hour_names[12] = {
        "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"
    };
    uint8_t n = 0;
    xm_pages_hl_reset();
    if (!s_state.hl_valid) {
        s_state.hl_head_n = 1u;
        s_state.hl_col_split = 1u;
        xm_pages_copy(s_state.hl_lines[n++], 128, xm_pages_hl_missing_text());
    } else {
        /* 两栏各 6 个时辰，112px 栏宽下每行 82px，16px 字清楚可辨。
         * 「当前」不再靠文字后缀（会撑破栏宽），改由 VM 用朱砂色 +
         * 一行「朱砂为当前时辰」说明，信息不丢且更醒目。 */
        for (uint8_t i = 0; i < 12 && n < 14; ++i) {
            const char *luck = (s_state.hl.hour_luck & (1u << i)) ? "吉" : "凶";
            xm_pages_fmt(s_state.hl_lines[n++], 128, "%s %s %s",
                         s_hour_names[i], s_state.hl.hour_gz[i], luck);
        }
        s_state.hl_head_n = 0u;
        s_state.hl_col_split = 6u;
    }
    s_state.hl_line_n = n;
}

/* 六爻结果页按 sub 分九屏填充，屏幕太小放不下全部解读，故拆屏让用户翻看：
 *   sub=0 卦象：本卦 / 之卦 / 卦辞 / 变卦卦辞（核心卦面）
 *   sub=1 动爻：列出本卦动爻爻辞（静卦时给出提示）
 *   sub=2 总论：整卦综合解读（就卦象讲的整体判断）
 *   sub=3 事业 | sub=4 财富 | sub=5 爱情 | sub=6 健康 | sub=7 家宅
 *        —— 用户真正要的「解卦」：分生活维度讲卦象给出的启示
 *   sub=8 占断：宜 / 忌 / 时机（传统占断，保留）
 * 每屏只放一个主题，正文折行后视觉行 ≤ 8（XM_VM_LY_BODY_ROWS），不会溢出单屏。
 * 单维度文本最长约 80 字，16px 下每行约 13 全角字，折行 ≤ 7 行，单屏可完整显示。 */
#define XM_LY_SUB_COUNT 9u
static void xm_pages_fill_result(void) {
    uint8_t n = 0;
    const char *ben = xm_gua_name(s_state.ly.ben_idx);
    const char *changed = xm_gua_name(s_state.ly.changed_idx);
    const char *guaci = xm_gua_text(s_state.ly.ben_idx, XM_YJ_GUACI);
    const char *changed_guaci = xm_gua_text(s_state.ly.changed_idx, XM_YJ_GUACI);

    if (!ben || !changed || !guaci || !changed_guaci) {
        xm_pages_copy(s_state.result_lines[n++], 160, XM_ERR_E5);
        s_state.result_line_n = n;
        return;
    }

    const uint8_t sub = s_state.sub < XM_LY_SUB_COUNT ? s_state.sub : 0u;
    switch (sub) {
        case 0u: /* 卦象：本卦 / 之卦 / 卦辞 / 变卦卦辞 */
            xm_pages_fmt(s_state.result_lines[n++], 160, "本卦：%s", ben);
            xm_pages_fmt(s_state.result_lines[n++], 160, "之卦：%s%s", changed,
                         s_state.ly.is_static ? " · 静卦无动" : "");
            xm_pages_fmt(s_state.result_lines[n++], 160, "卦辞：%s", guaci);
            if (!s_state.ly.is_static) {
                xm_pages_fmt(s_state.result_lines[n++], 160, "变卦卦辞：%s", changed_guaci);
            }
            break;
        case 1u: /* 动爻 */
            if (s_state.ly.is_static) {
                xm_pages_copy(s_state.result_lines[n++], 160, "本卦为静卦，无动爻");
                xm_pages_copy(s_state.result_lines[n++], 160, "以本卦卦辞大象为准");
            } else {
                for (uint8_t i = 0; i < 6u && n < 8u; ++i) {
                    if ((s_state.ly.moving_mask & (1u << i)) == 0u) continue;
                    const char *label = xm_yao_label(s_state.ly.ben_mark, (uint8_t)(i + 1u));
                    const char *yao = xm_gua_text(s_state.ly.ben_idx, (uint8_t)(XM_YJ_YAO_1 + i));
                    if (yao) {
                        xm_pages_fmt(s_state.result_lines[n++], 160, "%s爻 %s：%s",
                                     i == 0u ? "初" : (i == 5u ? "上" : "动"), label, yao);
                    }
                }
            }
            break;
        case 2u: /* 总论：整卦综合解读 */
        case 3u: /* 事业 */
        case 4u: /* 财富 */
        case 5u: /* 爱情 */
        case 6u: /* 健康 */
        case 7u: /* 家宅 */
        default: { /* sub 已 clamp 到 0..8，default 仅为防御性兜底（按家宅处理） */
            /* sub-2 映射到字段 OVERALL(13)..HOME(18)：2->13, 3->14 ... 7->18。 */
            const uint8_t field = (uint8_t)(XM_YJ_OVERALL + (sub - 2u));
            const char *text = xm_gua_text(s_state.ly.ben_idx, field);
            xm_pages_copy(s_state.result_lines[n++], 160, text ? text : "（暂缺）");
            break;
        }
        case 8u: /* 占断：宜 / 忌 / 时机（传统占断，保留） */
            n = 0u;
            {
                const char *yi = xm_gua_text(s_state.ly.ben_idx, XM_YJ_YI);
                const char *ji = xm_gua_text(s_state.ly.ben_idx, XM_YJ_JI);
                const char *timing = xm_gua_text(s_state.ly.ben_idx, XM_YJ_TIMING);
                xm_pages_fmt(s_state.result_lines[n++], 160, "宜：%s", yi ? yi : "（暂缺）");
                xm_pages_fmt(s_state.result_lines[n++], 160, "忌：%s", ji ? ji : "（暂缺）");
                xm_pages_fmt(s_state.result_lines[n++], 160, "时机：%s", timing ? timing : "（暂缺）");
            }
            break;
    }
    s_state.result_line_n = n;
}

static void xm_pages_fill_history(const xm_store_t *store) {
    s_state.history_count = store->hist.count;
    s_state.history_empty = (store->hist.count == 0);
    for (uint8_t i = 0; i < store->hist.count && i < 8; ++i) {
        const xm_hist_rec_t rec = xm_hist_at(&store->hist, i);
        const xm_date_t date = xm_hist_unpack_time(rec.dev_time);
        const char *ben = xm_gua_name(rec.ben_idx);
        const char *changed = xm_gua_name(rec.changed_idx);
        xm_pages_fmt(s_state.history_text[i], 80, "%04u-%02u-%02u %02u:%02u %s→%s",
                     (unsigned)date.year, (unsigned)date.month, (unsigned)date.day,
                     (unsigned)date.hour, (unsigned)date.minute,
                     ben ? ben : "?", changed ? changed : "?");
    }
}

static void xm_pages_fill_about(void) {
    /* 正文字号 12→16（224px 全宽最多 14 个全角字，ASCII 半宽）。
     * 10 行文案逐条重新断句，每条都 <=224px；固定行距 20px，
     * 末行 y=234、底边 252，不压按键区(264)。信息一条不删，只是把
     * 原先挤在一行里的两句拆开/合并，避免超宽被固定高度 label 裁掉。 */
    static const char *const s_lines[10] = {
        "本程序仅供传统文化研究参考",
        "不伪造引文 不承诺结果",
        "不采集个人信息 不联网",
        "数据：黄历查表 64卦 整理本",
        "卦辞：《周易》古经",
        "白话断语为今人整理",
        "时间：设备本地时钟 未授时",
        "常亮 P1 P2 仅记录不实现",
        "玩法版本 1.0.0",
        "构建 占位"
    };
    for (uint8_t i = 0; i < 10; ++i) {
        xm_pages_copy(s_state.about_lines[i], 160, s_lines[i]);
    }
    /* 构建标识最长截到 8 个 ASCII 字符：16px 下再多就会超出 224px 被裁。 */
    xm_pages_fmt(s_state.about_lines[9], 160, "构建 %.8s", s_state.build_id);
    s_state.about_line_n = 10;
}

xm_view_t xm_pages_render(const xm_nav_t *nav,
                          const xm_time_t *time,
                          const xm_store_t *store,
                          const xm_liuyao_t *ly,
                          const uint8_t toss[6],
                          const xm_hl_table_t *hl_table,
                          int64_t now_us,
                          int soc,
                          uint8_t err_flags) {
    xm_vm_state_init(&s_state);
    s_state.page = nav->page;
    s_state.sel = nav->sel;
    s_state.sub = nav->sub;
    s_state.cast_count = nav->cast_count;
    s_state.cast_phase = nav->cast_phase;
    s_state.cast_frame = nav->cast_frame;
    s_state.notice = nav->notice;
    s_state.review_mode = nav->review_mode;
    s_state.settings_confirm_clear = nav->confirm_clear;
    s_state.battery_soc = soc;
    s_state.err_flags = err_flags;
    s_state.time_cursor = (uint8_t)(nav->time_cursor % 5u);
    if (ly) s_state.ly = *ly;
    if (toss) memcpy(s_state.cast_toss, toss, 6);
    const xm_datetime_t now = xm_time_now(time, now_us);
    s_state.datetime = now;
    (void)xm_time_format(&now, s_state.date_text, sizeof(s_state.date_text),
                         s_state.time_text, sizeof(s_state.time_text));
    /* 授时页分段高亮要按字段单独排版，这里把 5 段文本拆好（各自独立缓冲，
     * xm_view_t 只存 const char*，指向 date_text 中间会被截断）。
     *
     * 必须显示「正在编辑的草稿」nav->draft_date，不能显示系统时钟 now：
     * 上/下键改的是 draft_date，旧版这里全部从 now 取，数值改了屏幕却纹丝不动，
     * 真机表现就是「授时按上下键时间无法调整」。黄历/首页等仍用 now，
     * 只有授时页的这 5 段属于草稿编辑区。 */
    const xm_date_t *const draft = &nav->draft_date;
    (void)snprintf(s_state.time_field_text[0], sizeof(s_state.time_field_text[0]),
                   "%04u", (unsigned)draft->year);
    (void)snprintf(s_state.time_field_text[1], sizeof(s_state.time_field_text[1]),
                   "%02u", (unsigned)draft->month);
    (void)snprintf(s_state.time_field_text[2], sizeof(s_state.time_field_text[2]),
                   "%02u", (unsigned)draft->day);
    (void)snprintf(s_state.time_field_text[3], sizeof(s_state.time_field_text[3]),
                   "%02u", (unsigned)draft->hour);
    (void)snprintf(s_state.time_field_text[4], sizeof(s_state.time_field_text[4]),
                   "%02u", (unsigned)draft->minute);
    s_state.hour_index = (uint8_t)(((now.date.hour + 1u) / 2u) % 12u);
    if (!xm_time_in_range(&now.date)) s_state.err_flags |= XM_ERR_HL_EXPIRED_BIT;
    if (xm_hl_lookup(hl_table, &now.date, &s_state.hl) != XM_OK) {
        s_state.hl_valid = false;
        s_state.err_flags |= XM_ERR_CONTENT_MISSING_BIT;
    } else {
        s_state.hl_valid = true;
        xm_pages_fmt(s_state.today_line, sizeof(s_state.today_line), "今日干支 %s %s %s",
                     s_state.hl.gz_y, s_state.hl.gz_m, s_state.hl.gz_d);
    }
    /* 顶部进度：还没摇显示「第 1 爻」，摇动中显示当前爻，落定后预告下一爻。 */
    unsigned shown = (unsigned)nav->cast_count;
    if (shown == 0u) shown = 1u;
    else if (nav->cast_phase == 0u && shown < 6u) shown += 1u;
    if (shown > 6u) shown = 6u;
    xm_pages_fmt(s_state.cast_text, sizeof(s_state.cast_text), "第 %u / 6 爻", shown);
    /* 摇卦页唯一的操作答案：动画中 / 下一爻 / 看卦象，三种状态都写明按哪个键。 */
    if (nav->cast_phase == 1u) {
        xm_pages_fmt(s_state.cast_prompt, sizeof(s_state.cast_prompt), "%s %s",
                     XM_CAST_OK, s_state.cast_text);
    } else if (nav->cast_count >= 6u) {
        xm_pages_copy(s_state.cast_prompt, sizeof(s_state.cast_prompt), XM_CAST_DONE);
    } else {
        xm_pages_fmt(s_state.cast_prompt, sizeof(s_state.cast_prompt),
                     "%s %u %s", XM_CAST_NEXT_PREFIX,
                     (unsigned)(nav->cast_count + 1u), XM_CAST_NEXT_SUFFIX);
    }
    /* 授时页进度：确定键逐段推进，屏上看得到「第 N / 5 段」。 */
    xm_pages_fmt(s_state.time_progress, sizeof(s_state.time_progress),
                 "第 %u / 5 段", (unsigned)(s_state.time_cursor + 1u));
    /* 只算当前页需要的那一张黄历子页：三张各自全量重写 hl_lines[]，
     * 以前这里连算三张再重算一次，既浪费 CPU，又让残留缓冲把「宜/忌」前缀吃掉。 */
    if (nav->page == XM_PAGE_HL_C) xm_pages_fill_hl_c();
    else if (nav->page == XM_PAGE_HL_B) xm_pages_fill_hl_b();
    else xm_pages_fill_hl_a();
    xm_pages_fill_result();
    xm_pages_fill_history(store);
    xm_pages_fill_about();
    return xm_vm_render(nav->page, &s_state);
}
