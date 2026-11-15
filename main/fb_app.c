/* 折叠北京 · 页面状态机与渲染
 *
 * 三键语义（全应用统一）：
 *   UP / DOWN : 在当前页的可选项之间移动
 *   OK 单击   : 确认 / 进入 / 执行
 *   OK 长按   : 返回上一级（首页再长按无动作）
 *
 * 布局：240x320，顶栏 0~30（标题 + 右上电量），内容 34~284，底部提示 292~316。
 * 所有上屏文本先进 app->text_pool 再挂到 xm_view_line_t.text，
 * 因为 xm_ui 用 lv_label_set_text_static() 保存指针。
 */

#include "fb_app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "fb_art.h"
#include "xm_fontcov.h"
#include "xm_vm.h"

/* ---------- 视觉规范 ----------
 *
 * 版式（240x320，自顶向下）：
 *   0..24    状态条：场景图标 + 日期，右上留给 xm_status 的电量
 *   26..130  场景横幅 240x104（RGB565 直读 Flash，零 RAM）
 *   134..286 内容区（高度 152，按页决定卡片排布）
 *   298      底部键位提示
 *
 * 视觉语言对齐官方 ui_pixel 与社区已上架玩法：高饱和、硬边缘、粗边框、
 * 图标 sprite 压在任何底色上。不做柔和阴影与半透明 —— 240x320 上看不清。
 */
#define FB_COLOR_ACCENT  0xE07A3Fu   /* 霓虹橙：选中/强调 */
#define FB_COLOR_WARN    0xE8B44Cu   /* 琥珀：欠款/警告 */
#define FB_COLOR_SELBG   0x232A38u   /* 选中项底块 */
#define FB_COLOR_CARD    0x1A1F2Au   /* 卡片底 */
#define FB_COLOR_BAR_BG  0x232A38u   /* 进度条槽 */

#define FB_STATUS_H   26
#define FB_SCENE_Y    26
#define FB_SCENE_H    104
#define FB_CONTENT_Y  134

#define FB_ROW_H     24
#define FB_SMALL_W   17   /* 12px 一行可容纳的 CJK 字数（224/12 ≈ 18，留 1 字余量） */
#define FB_BODY_W    13   /* 16px 一行可容纳的 CJK 字数（224/16 = 14，留 1 字余量） */

/* 场景图 id：与 fb_art.h 生成的数组一一对应 */
typedef enum {
    FB_SC_INTRO = 0, FB_SC_HOME, FB_SC_MARKET, FB_SC_DELIVERY,
    FB_SC_GIG, FB_SC_RIDE, FB_SC_LIVE, FB_SC_REPORT,
    FB_SC_EVENT, FB_SC_ENDING, FB_SC_COUNT
} fb_scene_id_t;

typedef struct {
    const uint16_t *data;
    uint16_t w;
    uint16_t h;
} fb_scene_t;

/* 场景与图标都通过 switch 取。不能做成静态 const 表 ——
 * fb_art 里的符号是链接期才定址的指针，不是编译期常量。 */
static fb_scene_t scene_of(fb_scene_id_t id) {
    switch (id) {
        case FB_SC_INTRO:
            return (fb_scene_t){fb_scene_intro_room, FB_SCENE_INTRO_ROOM_W, FB_SCENE_INTRO_ROOM_H};
        case FB_SC_HOME:
            return (fb_scene_t){fb_scene_home_street, FB_SCENE_HOME_STREET_W, FB_SCENE_HOME_STREET_H};
        case FB_SC_MARKET:
            return (fb_scene_t){fb_scene_market_stall, FB_SCENE_MARKET_STALL_W, FB_SCENE_MARKET_STALL_H};
        case FB_SC_DELIVERY:
            return (fb_scene_t){fb_scene_delivery_road, FB_SCENE_DELIVERY_ROAD_W, FB_SCENE_DELIVERY_ROAD_H};
        case FB_SC_GIG:
            return (fb_scene_t){fb_scene_gig_warehouse, FB_SCENE_GIG_WAREHOUSE_W, FB_SCENE_GIG_WAREHOUSE_H};
        case FB_SC_RIDE:
            return (fb_scene_t){fb_scene_ride_car, FB_SCENE_RIDE_CAR_W, FB_SCENE_RIDE_CAR_H};
        case FB_SC_LIVE:
            return (fb_scene_t){fb_scene_live_room, FB_SCENE_LIVE_ROOM_W, FB_SCENE_LIVE_ROOM_H};
        case FB_SC_REPORT:
            return (fb_scene_t){fb_scene_report_night, FB_SCENE_REPORT_NIGHT_W, FB_SCENE_REPORT_NIGHT_H};
        case FB_SC_EVENT:
            return (fb_scene_t){fb_scene_event_rain, FB_SCENE_EVENT_RAIN_W, FB_SCENE_EVENT_RAIN_H};
        case FB_SC_ENDING:
            return (fb_scene_t){fb_scene_ending_dawn, FB_SCENE_ENDING_DAWN_W, FB_SCENE_ENDING_DAWN_H};
        default:
            return (fb_scene_t){NULL, 0, 0};
    }
}

static const uint8_t *icon_of(fb_icon_id_t id) {
    switch (id) {
        case FB_ICO_CASH: return fb_icon_cash;
        case FB_ICO_HEART: return fb_icon_heart;
        case FB_ICO_BOLT: return fb_icon_bolt;
        case FB_ICO_STAR: return fb_icon_star;
        case FB_ICO_CAL: return fb_icon_cal;
        case FB_ICO_WORK: return fb_icon_work;
        case FB_ICO_REST: return fb_icon_rest;
        case FB_ICO_CARD: return fb_icon_card;
        case FB_ICO_BOX: return fb_icon_box;
        case FB_ICO_DOOR: return fb_icon_door;
        case FB_ICO_INFO: return fb_icon_info;
        case FB_ICO_CLOCK: return fb_icon_clock;
        default: return NULL;
    }
}

/* ---------- 文本池 ---------- */

static const char *pool_put(fb_app_t *app, const char *fmt, ...) {
    if (app->pool_used >= FB_TEXT_ROWS) return "";
    char *dst = app->text_pool[app->pool_used++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, FB_TEXT_COLS, fmt, ap);
    va_end(ap);
    return dst;
}

static const char *pool_raw(fb_app_t *app, const char *src) {
    if (!src) src = "";
    return pool_put(app, "%s", src);
}

/* 按 CJK 字数截断后入池。新闻这类外部文案长度不定，
 * 直接丢给 232px 宽的 label 只会溢出到屏幕外，必须自己先切。 */
static const char *pool_clip(fb_app_t *app, const char *src, uint8_t max_chars) {
    if (app->pool_used >= FB_TEXT_ROWS) return "";
    char *dst = app->text_pool[app->pool_used++];
    size_t n = 0;
    uint8_t count = 0;
    while (src && *src && count < max_chars) {
        uint8_t len = xm_utf8_char_len((uint8_t)*src);
        if (len == 0) len = 1;
        if (n + len + 1 >= FB_TEXT_COLS) break;
        memcpy(dst + n, src, len);
        n += len;
        src += len;
        ++count;
    }
    dst[n] = '\0';
    return dst;
}

/* ---------- 视图辅助 ---------- */

static void v_line(xm_view_t *v, int x, int y, uint16_t w, uint8_t size,
                   uint32_t color, uint8_t align, const char *text) {
    if (v->line_n >= XM_VIEW_LINE_MAX || !text) return;
    xm_view_line_t *l = &v->lines[v->line_n++];
    l->x = (int16_t)x;
    l->y = (int16_t)y;
    l->w = w;
    l->size = size;
    l->color = color;
    l->align = align;
    l->text = text;
}

static void v_rect(xm_view_t *v, int x, int y, uint16_t w, uint16_t h,
                   uint32_t color, bool fill) {
    if (v->box_n >= XM_VIEW_BOX_MAX) return;
    xm_view_box_t *b = &v->boxes[v->box_n++];
    b->x = (int16_t)x;
    b->y = (int16_t)y;
    b->w = w;
    b->h = h;
    b->color = color;
    b->fill = fill;
    b->border = !fill;
    b->radius = 0;
}

static void v_icon(xm_view_t *v, fb_icon_id_t id, int x, int y) {
    if (id <= FB_ICO_NONE || id >= FB_ICO_COUNT) return;
    if (v->icon_n >= XM_VIEW_ICON_MAX) return;
    xm_view_icon_t *ic = &v->icons[v->icon_n++];
    ic->x = (int16_t)x;
    ic->y = (int16_t)y;
    ic->data = icon_of(id);
    ic->w = FB_ICON_SIZE;
    ic->h = FB_ICON_SIZE;
    ic->id = (uint8_t)id;
}

/* 进度条：槽 + 填充。数值类信息靠长度读比靠数字读快，
 * 240x320 上这是唯一能让玩家"扫一眼就懂"的形式。 */
static void draw_bar(xm_view_t *v, int x, int y, int w, int h, int pct,
                     uint32_t color) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h, FB_COLOR_BAR_BG, true);
    int fill = w * pct / 100;
    if (fill > 0) v_rect(v, x, y, (uint16_t)fill, (uint16_t)h, color, true);
}

/* 顶部场景横幅。整幅 240x104 直接指向 Flash，不做解码也不做缩放。 */
static void draw_scene(xm_view_t *v, fb_scene_id_t id) {
    if (id >= FB_SC_COUNT) return;
    fb_scene_t sc = scene_of(id);
    v->image = sc.data;
    v->image_x = 0;
    v->image_y = FB_SCENE_Y;
    v->image_w = sc.w;
    v->image_h = sc.h;
    v->image_id = (uint8_t)id;
    /* 场景图下沿压一条深线，把图和内容区分开，避免文字像浮在画里 */
    v_rect(v, 0, FB_SCENE_Y + FB_SCENE_H, 240, 2, 0x090C12u, true);
}

/* 一天的账本角标：上面一行是干了几小时，下面一行是今晚能睡几小时 + 白天困不困。
 * 240x320 的内容区被数据卡和菜单吃满，这两条硬信息只能压进场景图 ——
 * 深底叠在画面右下角，十张场景图在那个位置都是暗部，压上去不遮主体。
 * 工时行：没干满橙色，24 小时干满转红；睡眠行：睡够青、犯困黄、撑不住红。 */
static void draw_hours(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    uint8_t left = fb_hours_left(st);
    uint8_t sleep = fb_sleep_hours(st);
    int x = 130;
    int y = FB_SCENE_Y + FB_SCENE_H - 36;   /* 94..126，落在场景图内 */
    v_rect(v, x, y, 104, 30, 0x0B0E14u, true);

    uint32_t work_fg = left == 0 ? XM_COLOR_RED : FB_COLOR_ACCENT;
    v_line(v, x + 5, y + 2, 94, 12, work_fg, XM_ALIGN_LEFT,
           pool_put(app, "已干 %u 小时", (unsigned)st->hours_used));

    /* 颜色和文字双通道：睡够灰、缺觉黄、危险红，同时把瞌睡等级写在后面 ——
     * 状态条只有 12px 一行，颜色看趋势、文字看等级，两个都得有。 */
    uint32_t sleep_fg = XM_COLOR_SUBINK;
    if (sleep < FB_SLEEP_CRIT) sleep_fg = XM_COLOR_RED;
    else if (sleep < FB_SLEEP_OK || st->drowsy != FB_DROWSY_NONE) sleep_fg = FB_COLOR_WARN;
    v_line(v, x + 5, y + 16, 94, 12, sleep_fg, XM_ALIGN_LEFT,
           pool_put(app, "睡 %u 时 · %s", (unsigned)sleep,
                    fb_drowsy_label(st->drowsy)));
}

/* 状态条：图标 + 日期。右上角电量由 xm_status 负责，这里只用到 x=176。 */
static void draw_status(fb_app_t *app, xm_view_t *v, fb_icon_id_t icon,
                        const char *text) {
    v_icon(v, icon, 6, 5);
    v_line(v, 26, 4, 150, 16, XM_COLOR_INK, XM_ALIGN_LEFT,
           pool_raw(app, text));
}

/* 卡片：硬边框 + 选中底块 + 左侧强调竖条。
 * 选中态不用箭头符号（思源黑体对 ▲▼ 覆盖不稳），改用块面变化。 */
static void draw_card(fb_app_t *app, xm_view_t *v, int x, int y, int w, int h,
                      bool selected, bool enabled, fb_icon_id_t icon,
                      const char *text) {
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h,
           selected && enabled ? FB_COLOR_SELBG : FB_COLOR_CARD, true);
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h,
           selected && enabled ? FB_COLOR_ACCENT : XM_COLOR_LINE, false);
    int tx = x + 8;
    if (selected && enabled) {
        v_rect(v, x, y, 3, (uint16_t)h, FB_COLOR_ACCENT, true);
        tx += 2;
    }
    if (icon > FB_ICO_NONE) {
        v_icon(v, icon, tx, y + (h - (int)FB_ICON_SIZE) / 2);
        tx += (int)FB_ICON_SIZE + 6;
    }
    uint32_t color = !enabled ? XM_COLOR_DISABLE
                              : (selected ? FB_COLOR_ACCENT : XM_COLOR_INK);
    /* 展开态（h >= 32）文字靠上，把下半张卡让给调用方画描述；
     * 普通高度居中。这样展开/收起时文字不会上下跳太多。 */
    int ty = (h >= 32) ? y + 3 : y + (h - 16) / 2;
    v_line(v, tx, ty, (uint16_t)(x + w - tx - 6), 16, color,
           XM_ALIGN_LEFT, pool_raw(app, text));
}

static void draw_hint(fb_app_t *app, xm_view_t *v, const char *hint) {
    v_line(v, 8, 300, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_CENTER,
           pool_raw(app, hint));
}

/* 自动换行的整段文本：按 CJK 字数切，返回占用行数 */
static uint8_t draw_para(fb_app_t *app, xm_view_t *v, int x, int y,
                         const char *text, uint8_t chars_per_line,
                         uint8_t line_h, uint8_t size, uint32_t color,
                         uint8_t max_lines) {
    if (!text) return 0;
    uint8_t rows = 0;
    const char *p = text;
    while (*p && rows < max_lines) {
        char buf[FB_TEXT_COLS];
        size_t n = 0;
        uint8_t count = 0;
        while (*p && count < chars_per_line) {
            uint8_t len = xm_utf8_char_len((uint8_t)*p);
            if (len == 0) len = 1;
            if (n + len >= sizeof(buf)) break;
            if (*p == '\n') { ++p; break; }
            memcpy(buf + n, p, len);
            n += len;
            p += len;
            ++count;
        }
        buf[n] = '\0';
        v_line(v, x, y + rows * line_h, 224, size, color, XM_ALIGN_LEFT,
               pool_put(app, "%s", buf));
        ++rows;
    }
    return rows;
}

/* ---------- 各行文案 ---------- */

static const char *menu_label(uint8_t idx) {
    switch (idx) {
        case FB_MENU_WORK: return "去干活";
        case FB_MENU_REST: return "歇一天";
        case FB_MENU_FINANCE: return "借钱";
        case FB_MENU_STORAGE: return "囤货";
        case FB_MENU_ENDDAY: return "收工";
        case FB_MENU_ABOUT: return "说明";
        default: return "";
    }
}

static fb_icon_id_t menu_icon(uint8_t idx) {
    switch (idx) {
        case FB_MENU_WORK: return FB_ICO_WORK;
        case FB_MENU_REST: return FB_ICO_REST;
        case FB_MENU_FINANCE: return FB_ICO_CARD;
        case FB_MENU_STORAGE: return FB_ICO_BOX;
        case FB_MENU_ENDDAY: return FB_ICO_DOOR;
        case FB_MENU_ABOUT: return FB_ICO_INFO;
        default: return FB_ICO_NONE;
    }
}

static fb_icon_id_t line_icon(uint8_t line) {
    switch (line) {
        case FB_LINE_MARKET: return FB_ICO_WORK;
        case FB_LINE_DELIVERY: return FB_ICO_BOX;
        case FB_LINE_GIG: return FB_ICO_BOLT;
        case FB_LINE_RIDE: return FB_ICO_CARD;
        case FB_LINE_LIVE: return FB_ICO_STAR;
        default: return FB_ICO_NONE;
    }
}

/* ---------- 页面渲染 ---------- */

static void render_intro(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_bg_t *bgs = fb_bg_table(&n);
    draw_scene(v, FB_SC_INTRO);

    v_line(v, 8, 136, 224, 24, XM_COLOR_INK, XM_ALIGN_CENTER,
           pool_raw(app, "折叠北京"));
    v_line(v, 8, 164, 224, 12, FB_COLOR_ACCENT, XM_ALIGN_CENTER,
           pool_raw(app, "三层空间 · 一百天"));

    /* 有存档时多出「继续」一项，把三个开局压到 28 行距（24 高）
     * 才塞得进 292 以内；没存档就保持原来的 32 行距。 */
    if (app->have_save) {
        draw_card(app, v, 6, 178, 228, 30, app->sel == 0, true, FB_ICO_DOOR,
                  pool_put(app, "继续上次：第 %d 天", (int)app->save_day));
        for (uint8_t i = 0; i < (uint8_t)n && i < 3; ++i) {
            draw_card(app, v, 6, 212 + i * 26, 228, 24, app->sel == i + 1, true,
                      FB_ICO_CASH,
                      pool_put(app, "%s ¥%d", bgs[i].name, (int)bgs[i].cash));
        }
        draw_hint(app, v, "↑↓ 选    OK 确定");
    } else {
        for (uint8_t i = 0; i < (uint8_t)n && i < 3; ++i) {
            draw_card(app, v, 6, 186 + i * 32, 228, 28, app->bg_sel == i, true,
                      FB_ICO_CASH,
                      pool_put(app, "%s    现金 ¥%d", bgs[i].name,
                               (int)bgs[i].cash));
        }
        draw_hint(app, v, "↑↓ 选开局    OK 开始");
    }
}

static void render_home(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_status(app, v, FB_ICO_CAL, pool_put(app, "第 %d 天 / 100", st->day));
    draw_scene(v, FB_SC_HOME);
    draw_hours(app, v);

    /* 今日新闻：主面板是每天必看的一屏，北京的天气和行情得在这儿交代。
     * 内容区被数据卡和菜单吃满，只能压一条 18px 深底在场景图上沿 ——
     * 十张场景图顶部都是天空或墙面，压上去不遮主体。 */
    v_rect(v, 0, FB_SCENE_Y, 240, 18, 0x0B0E14u, true);
    v_line(v, 4, FB_SCENE_Y + 3, 232, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_clip(app, fb_news_today(st), 18));

    /* 现金：大数字 + 图标，是玩家最常看的一个数 */
    v_icon(v, FB_ICO_CASH, 8, 137);
    v_line(v, 30, 133, 96, 24, st->cash < 100 ? FB_COLOR_WARN : XM_COLOR_INK,
           XM_ALIGN_LEFT, pool_put(app, "%d", (int)st->cash));
    v_line(v, 30, 159, 96, 12, st->debt > 0 ? FB_COLOR_WARN : XM_COLOR_SUBINK,
           XM_ALIGN_LEFT, pool_put(app, "欠债 %d", (int)st->debt));

    /* 健康 / 精力：条 + 数字。右边一列，和数据形成两栏 */
    v_icon(v, FB_ICO_HEART, 124, 137);
    draw_bar(v, 146, 141, 84, 8, st->health,
             st->health < 30 ? XM_COLOR_RED : XM_COLOR_BLUE);
    v_line(v, 146, 151, 84, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_put(app, "健康 %d", st->health));

    v_icon(v, FB_ICO_BOLT, 124, 159);
    draw_bar(v, 146, 163, 84, 8, st->stamina,
             st->stamina < 20 ? FB_COLOR_WARN : FB_COLOR_ACCENT);
    v_line(v, 146, 173, 84, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_put(app, "精力 %d", st->stamina));

    /* 菜单：2 列 x 3 行卡片。纯文字列表在小屏上没有"可以点"的暗示 */
    for (uint8_t i = 0; i < FB_MENU_COUNT; ++i) {
        int col = i % 2, row = i / 2;
        draw_card(app, v, 6 + col * 118, 188 + row * 34, 110, 30,
                  app->sel == i, true, menu_icon(i), pool_raw(app, menu_label(i)));
    }
    /* 底部这一行优先给「上一次干活的结果」：一天能连打几份工，
     * 玩家必须看到上一单挣了多少、扣了什么，否则连干三份是在盲赌。 */
    draw_hint(app, v, app->toast[0] ? app->toast : "↑↓←→ 选择    OK 确定");
}

static void render_lines(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_line_meta_t *meta = fb_line_table(&n);
    draw_status(app, v, FB_ICO_WORK, "去哪儿挣钱");
    draw_scene(v, FB_SC_HOME);
    draw_hours(app, v);

    /* 选中项展开成两行卡片，其余压到 26。五条线全展开需要 5x40=200，
     * 152 的内容区放不下，展开式让描述有地方待又不挤。 */
    int y = 134;
    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        bool unlocked = fb_line_unlocked(&app->st, i);
        bool sel = app->sel == i;
        int h = sel ? 42 : 26;
        draw_card(app, v, 6, y, 228, h, sel, unlocked, line_icon(i),
                  pool_raw(app, meta[i].name));
        if (sel) {
            v_line(v, 33, y + 21, 195, 12,
                   unlocked ? XM_COLOR_SUBINK : XM_COLOR_DISABLE, XM_ALIGN_LEFT,
                   pool_raw(app, unlocked ? meta[i].desc : fb_unlock_hint(i)));
        }
        y += h + 2;
    }
    draw_hint(app, v, "↑↓ 选行当    OK 进入");
}

/* 摆摊页：点位 + 五种货 + 出摊
 *
 * 第一项是摆摊点位 —— 同一个摊子摆在夜市比摆在公园多卖三成，
 * 而每天的需求矩阵是开局就摇定的（只摇一次，不随切点位重摇）。
 * 所以「换点位」是读盘判断，不是刷骰子。 */
static void render_market(fb_app_t *app, xm_view_t *v) {
    size_t n = 0, ns = 0;
    const fb_item_t *items = fb_item_table(&n);
    const fb_spot_t *spots = fb_spot_table(&ns);
    const fb_state_t *st = &app->st;
    uint8_t spot = st->spot < ns ? st->spot : 0;
    draw_status(app, v, FB_ICO_BOX,
                pool_put(app, "库存 %u/%u", st->inv_total, st->storage));
    draw_scene(v, FB_SC_MARKET);
    draw_hours(app, v);

    /* 7 项要塞进 134..284：每项 21 行距（20 高），出摊卡 24 高。
     * 早先 24 行距 + 5 货 + 出摊 = 150，加上点位行就压到提示行了。 */
    draw_card(app, v, 6, 134, 228, 20, app->sel == 0, true, FB_ICO_CAL,
              pool_put(app, "点位 %s ×%u%%", spots[spot].name,
                       spots[spot].bias_pct));

    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        int y = 155 + i * 21;
        bool can = st->cash >= items[i].cost && st->inv_total < st->storage;
        /* 需求写进卡片：进多少货不能靠猜，得看得见这个点今天能卖几份 */
        draw_card(app, v, 6, y, 228, 20, app->sel == i + 1, can, FB_ICO_BOX,
                  pool_put(app, "%s 进%d/卖%d 有%u/%u", items[i].name,
                           (int)items[i].cost, (int)items[i].price, st->inv[i],
                           (unsigned)fb_demand_at(st, spot, i)));
    }
    draw_card(app, v, 6, 260, 228, 24, app->sel == n + 1, true,
              FB_ICO_CASH, "出摊卖货");
    draw_hint(app, v, "OK 换点位/进货/出摊");
}

static void render_gig(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_gig_t *gigs = fb_gig_table(&n);
    draw_status(app, v, FB_ICO_BOLT, "日结零工");
    draw_scene(v, FB_SC_GIG);
    draw_hours(app, v);

    /* 6 个工种要塞进 134..286 这 152px：每项 23 高、行距 25，名称/工资/体力压成一行。
     * 早先按 42 行距排（每项占标题+描述两行），最后两项直接排到了屏幕外面。 */
    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        int y = 134 + i * 25;
        draw_card(app, v, 6, y, 228, 23, app->sel == i, true, FB_ICO_BOLT,
                  pool_put(app, "%s ¥%d · 精力%u", gigs[i].name,
                           (int)gigs[i].wage, gigs[i].stamina));
    }
    draw_hint(app, v, "↑↓ 选工种    OK 上工");
}

static void render_topic(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_topic_t *tp = fb_topic_table(&n);
    draw_status(app, v, FB_ICO_STAR, pool_put(app, "粉丝 %u", app->st.fans));
    draw_scene(v, FB_SC_LIVE);
    draw_hours(app, v);

    /* 卡片必须 h>=32 才会把标题靠上排（draw_card 的规则），
     * 否则标题(16px)会和描述(12px)叠成一团。
     * 4 张 32 高的卡 + 33 行距落在 134..265，下面留给连播状态。 */
    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        int y = 134 + i * 33;
        draw_card(app, v, 6, y, 228, 32, app->sel == i, true, FB_ICO_STAR,
                  pool_raw(app, tp[i].name));
        v_line(v, 32, y + 19, 196, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
               pool_put(app, "涨粉%d 风险%d%% · %s", tp[i].fans, tp[i].risk_pct,
                        tp[i].note));
    }
    /* 连播加成是这条线唯一需要「坚持」的机制，必须让玩家看见
     * 自己已经播了几天、再加成到多少 —— 看不见的加成等于没有。 */
    uint8_t bonus = fb_live_bonus_pct(&app->st);
    v_line(v, 8, 268, 224, 12, bonus > 100 ? FB_COLOR_ACCENT : XM_COLOR_SUBINK,
           XM_ALIGN_LEFT,
           pool_put(app, "连播 %u 天 · 涨粉 ×%u.%u", (unsigned)app->st.live_streak,
                    bonus / 100, (bonus % 100) / 10));
    draw_hint(app, v, "↑↓ 选题    OK 开播");
}

/* 外卖 / 网约车：装备 + 跑 */
static void render_vehicle(fb_app_t *app, xm_view_t *v, uint8_t line) {
    const fb_state_t *st = &app->st;
    bool unlocked = fb_line_unlocked(st, line);
    fb_icon_id_t ic = line == FB_LINE_DELIVERY ? FB_ICO_BOX : FB_ICO_CARD;
    draw_status(app, v, ic, line == FB_LINE_DELIVERY ? "送外卖" : "网约车");
    draw_scene(v, line == FB_LINE_DELIVERY ? FB_SC_DELIVERY : FB_SC_RIDE);
    draw_hours(app, v);

    if (!unlocked) {
        draw_para(app, v, 8, 140,
                  line == FB_LINE_DELIVERY
                      ? "先置办电动车和装备，才能接单。"
                      : "先交押金租辆车，才能跑活。",
                  FB_BODY_W, 22, 16, XM_COLOR_INK, 3);
        draw_card(app, v, 6, 212, 228, 30, app->sel == 0, true, ic,
                  pool_put(app, "置办装备 ¥%d",
                           line == FB_LINE_DELIVERY ? 2800 : 8000));
        if (st->cash < (line == FB_LINE_DELIVERY ? 2800 : 8000)) {
            v_line(v, 8, 248, 224, 12, FB_COLOR_WARN, XM_ALIGN_LEFT,
                   pool_raw(app, "钱不够，去首页「借钱」"));
        }
    } else {
        draw_card(app, v, 6, 150, 228, 32, app->sel == 0, true, ic,
                  line == FB_LINE_DELIVERY ? "开始跑单" : "出车跑活");
        v_line(v, 8, 190, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
               pool_raw(app, line == FB_LINE_DELIVERY
                                ? "按单结算，可能超时扣款"
                                : "按公里结算，可能吃罚单"));
        draw_bar(v, 8, 212, 224, 6, st->stamina,
                 st->stamina < 20 ? FB_COLOR_WARN : FB_COLOR_ACCENT);
        v_line(v, 8, 222, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
               pool_put(app, "精力 %d/100 · 还能干 %u 小时", st->stamina,
                        (unsigned)fb_hours_left(st)));
    }
    draw_hint(app, v, "OK 确定    长按返回");
}

/* 财务页：借钱 / 还债 / 存钱。5 张卡按 31 行距排在 140..291，
 * 与底部提示行（300）留 9px，再紧就会糊成一片。 */
static void render_finance(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_status(app, v, FB_ICO_CARD,
                pool_put(app, "现金%d 存%d", (int)st->cash, (int)st->savings));
    draw_scene(v, FB_SC_REPORT);

    draw_card(app, v, 6, 140, 228, 27, app->sel == 0, true, FB_ICO_CARD,
              "刷卡套现 2000 费3%");
    draw_card(app, v, 6, 171, 228, 27, app->sel == 1, true, FB_ICO_CASH,
              "网贷 1000 日息1%");
    draw_card(app, v, 6, 202, 228, 27, app->sel == 2, st->debt > 0, FB_ICO_CASH,
              pool_put(app, "还 500（现欠 %d）", (int)st->debt));
    draw_card(app, v, 6, 233, 228, 27, app->sel == 3, st->cash > 0, FB_ICO_BOX,
              "存 1000 进银行");
    draw_card(app, v, 6, 264, 228, 27, app->sel == 4, st->savings > 0,
              FB_ICO_BOX, pool_put(app, "取 1000（存 %d）", (int)st->savings));
    draw_hint(app, v, "↑↓ 选择    OK 确定");
}

static void render_storage(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_status(app, v, FB_ICO_BOX,
                pool_put(app, "仓储 %u 格", st->storage));
    draw_scene(v, FB_SC_GIG);

    draw_bar(v, 8, 142, 224, 10, (int)(st->inv_total * 100u / (st->storage ? st->storage : 1)),
             XM_COLOR_BLUE);
    v_line(v, 8, 156, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_put(app, "已存 %u / %u 格", st->inv_total, st->storage));
    draw_card(app, v, 6, 184, 228, 32, app->sel == 0, st->cash >= 600,
              FB_ICO_BOX, "扩容 8 格  ¥600");
    v_line(v, 8, 228, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_raw(app, "扩容后能一次进更多货，"));
    v_line(v, 8, 246, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_raw(app, "出摊卖得更多。"));
    draw_hint(app, v, "OK 扩容    长按返回");
}

static void render_report(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_status(app, v, FB_ICO_DOOR, pool_put(app, "第 %d 天 收工", st->day));
    draw_scene(v, FB_SC_REPORT);

    /* 结算用大数字：这一天到底挣了还是亏了，是玩家唯一的判断依据 */
    v_icon(v, FB_ICO_CASH, 8, 141);
    v_line(v, 30, 137, 200, 24,
           st->day_earned >= st->day_spent ? XM_COLOR_BLUE : XM_COLOR_RED,
           XM_ALIGN_LEFT, pool_put(app, "今日 %+d", (int)(st->day_earned - st->day_spent)));
    v_icon(v, FB_ICO_BOLT, 8, 169);
    v_line(v, 30, 167, 100, 16, XM_COLOR_BLUE, XM_ALIGN_LEFT,
           pool_put(app, "+%d", (int)st->day_earned));
    v_icon(v, FB_ICO_CARD, 124, 169);
    v_line(v, 146, 167, 84, 16, FB_COLOR_WARN, XM_ALIGN_LEFT,
           pool_put(app, "-%d", (int)st->day_spent));

    /* 这一天的账要连着睡眠一起交代：干了几小时、睡了几小时、欠几天觉、白天困不困。
     * 玩家熬夜换来的钱，必须在这里看到它的代价。 */
    v_line(v, 8, 188, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_put(app, "干了 %u 小时，睡 %u 小时",
                    (unsigned)st->hours_used, (unsigned)st->last_sleep));
    v_line(v, 8, 204, 224, 12,
           st->sleep_debt >= 4 ? XM_COLOR_RED
                               : (st->sleep_debt ? FB_COLOR_WARN : XM_COLOR_SUBINK),
           XM_ALIGN_LEFT,
           pool_put(app, "欠觉 %u 天 · 健康 %d · %s", (unsigned)st->sleep_debt,
                    st->health, fb_drowsy_label(st->drowsy)));

    /* 当日语录：亏了给丧的陪着丧，挣了给毒鸡汤扎一刀。这是《活着》的底色。 */
    int32_t net = st->day_earned - st->day_spent;
    uint8_t qk = fb_quote_daily_kind(st->day, 1, net);
    draw_para(app, v, 8, 222, fb_quote_daily(st->day, 1, net), FB_SMALL_W, 15, 12,
              qk == FB_QUOTE_SANG ? XM_COLOR_SUBINK : FB_COLOR_ACCENT, 2);

    /* 明细（吃饭/交通/房租/过劳……）塞不下就让 draw_para 截断 */
    draw_para(app, v, 8, 254, app->toast, FB_SMALL_W, 15, 12, XM_COLOR_INK, 2);
    draw_hint(app, v, "OK 继续");
}

static void render_event(fb_app_t *app, xm_view_t *v) {
    const fb_event_t *e = app->ev;
    if (!e) return;
    draw_status(app, v, FB_ICO_INFO, e->title);
    draw_scene(v, FB_SC_EVENT);

    draw_para(app, v, 8, 140, e->text, FB_BODY_W, 22, 16, XM_COLOR_INK, 3);
    for (uint8_t i = 0; i < e->choice_n && i < 2; ++i) {
        draw_card(app, v, 6, 214 + i * 38, 228, 34, app->ev_choice == i, true,
                  FB_ICO_NONE, e->choices[i].label);
    }
    draw_hint(app, v, "↑↓ 选    OK 定");
}

static void render_ending(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_scene(v, FB_SC_ENDING);

    v_line(v, 8, 138, 224, 24, FB_COLOR_ACCENT, XM_ALIGN_CENTER,
           pool_raw(app, fb_end_title(st->ending)));
    draw_para(app, v, 8, 168, fb_end_text(st->ending), FB_BODY_W, 22, 16,
              XM_COLOR_INK, 3);

    v_icon(v, FB_ICO_CASH, 8, 240);
    v_line(v, 30, 236, 200, 24, XM_COLOR_INK, XM_ALIGN_LEFT,
           pool_put(app, "净值 %d", (int)fb_net_worth(st)));
    v_line(v, 8, 264, 224, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_put(app, "撑到第 %d 天", st->day > FB_TOTAL_DAYS ? FB_TOTAL_DAYS : st->day));
    draw_hint(app, v, "OK 再来一局");
}

/* 说明页：7 行 × 13 字 = 91 字上限，多一个标点都会被截断。
 * 行距压到 20 才放得下多出来的两条机制说明（点位 / 存款 / 连播）。 */
static const char *k_help =
    "一百天，五条活路：摆摊、外卖、日结、网约车、直播。"
    "一天 24 小时：干了活就没得睡，睡不够掉健康，"
    "欠觉会犯困掉钱，再撑就晕倒。"
    "摆摊挑点位，存钱有日息，连播涨粉快。"
    "健康到零，故事结束。";

static void render_about(fb_app_t *app, xm_view_t *v) {
    draw_status(app, v, FB_ICO_INFO, "玩法说明");
    draw_scene(v, FB_SC_HOME);
    draw_para(app, v, 8, 140, k_help, FB_BODY_W, 20, 16, XM_COLOR_INK, 7);
    draw_hint(app, v, "长按返回");
}

/* ---------- 视图组装 ---------- */

void fb_app_render(fb_app_t *app, xm_view_t *out) {
    memset(out, 0, sizeof(*out));
    app->pool_used = 0;

    switch (app->scr) {
        case FB_SCR_INTRO: render_intro(app, out); break;
        case FB_SCR_HOME: render_home(app, out); break;
        case FB_SCR_LINES: render_lines(app, out); break;
        case FB_SCR_ACTION: {
            size_t n = 0;
            (void)fb_line_table(&n);
            switch (app->line_sel) {
                case FB_LINE_MARKET: render_market(app, out); break;
                case FB_LINE_DELIVERY:
                case FB_LINE_RIDE:
                    render_vehicle(app, out, app->line_sel);
                    break;
                case FB_LINE_GIG: render_gig(app, out); break;
                case FB_LINE_LIVE: render_topic(app, out); break;
                default: render_lines(app, out); break;
            }
            break;
        }
        case FB_SCR_FINANCE: render_finance(app, out); break;
        case FB_SCR_STORAGE: render_storage(app, out); break;
        case FB_SCR_REPORT: render_report(app, out); break;
        case FB_SCR_EVENT: render_event(app, out); break;
        case FB_SCR_ENDING: render_ending(app, out); break;
        case FB_SCR_ABOUT: render_about(app, out); break;
        default: break;
    }
    out->footer = false;
    out->overlay = 0;
}

/* xm_status.c 需要这个符号；折叠北京没有异常态提示条，恒返回空 */
const char *xm_vm_overlay_text(uint8_t flags) {
    (void)flags;
    return "";
}

/* ============================ 状态机 ============================ */

static uint8_t page_item_count(const fb_app_t *app) {
    switch (app->scr) {
        case FB_SCR_INTRO: return app->have_save ? 4 : 3;
        case FB_SCR_HOME: return FB_MENU_COUNT;
        case FB_SCR_LINES: return FB_LINE_COUNT;
        case FB_SCR_ACTION:
            switch (app->line_sel) {
                /* 点位 + 5 种货 + 出摊 */
                case FB_LINE_MARKET: return FB_KIND_COUNT + 2;
                case FB_LINE_GIG: return FB_GIG_COUNT;
                case FB_LINE_LIVE: return FB_TOPIC_COUNT;
                default: return 1;
            }
        case FB_SCR_FINANCE: return 5;
        case FB_SCR_STORAGE: return 1;
        case FB_SCR_EVENT: return app->ev ? app->ev->choice_n : 1;
        default: return 1;
    }
}

/* 该线今天还能不能干：前置条件不满足时停在原页并给理由。
 * 工时需求统一从 fb_line_hours() 取，不在这一层另抄一份数字。 */
static bool can_run(fb_app_t *app, uint8_t line) {
    const fb_state_t *st = &app->st;
    if (!fb_line_unlocked(st, line)) {
        snprintf(app->toast, sizeof(app->toast), "还没置办装备：%s",
                 fb_unlock_hint(line));
        return false;
    }
    uint8_t need = fb_line_hours(line);
    if (need == 0) return false;
    if (line == FB_LINE_MARKET && st->inv_total == 0) {
        snprintf(app->toast, sizeof(app->toast), "没进货，摊子支不起来");
        return false;
    }
    if (fb_hours_left(st) < need) {
        snprintf(app->toast, sizeof(app->toast),
                 "今天只剩 %u 小时，这活要 %u 小时",
                 (unsigned)fb_hours_left(st), (unsigned)need);
        return false;
    }
    return true;
}

/* 干完一份活不结算 —— 一天 24 小时可以拆成多份工（外卖 6 + 日结 8 + 摆摊 5 + 直播 3）。
 * 什么时候收工由玩家自己决定：多干一份就少睡一小时，账记在睡眠上，不在这里拦。
 * 干完回主面板，底部那一行正好显示这一单的结果。 */
static void back_to_home_after_work(fb_app_t *app) {
    app->scr = FB_SCR_HOME;
    app->home_sel = app->sel = FB_MENU_WORK;
}

/* 结算 → 抽事件 → 推进/结束。所有“今天到此为止”的入口共用。 */
static void finish_day(fb_app_t *app) {
    fb_end_day(&app->st, app->toast, sizeof(app->toast));
    app->scr = FB_SCR_REPORT;
    app->sel = 0;
}

static void advance_after_event(fb_app_t *app) {
    fb_advance_day(&app->st);
    /* 推进到新一天 = 一个安全的存档点：结算已完成、事件已选完，
     * 此刻的 st 读回来不会重复结算任何一笔钱。 */
    app->save_dirty = true;
    if (fb_check_end(&app->st)) {
        app->scr = FB_SCR_ENDING;
    } else {
        /* 新的一天开头不保留昨天那一单的流水，但欠觉这件事必须继续顶在眼前 */
        app->toast[0] = '\0';
        if (app->st.sleep_debt >= 4) {
            snprintf(app->toast, sizeof(app->toast), "欠觉 %u 天，人快垮了",
                     (unsigned)app->st.sleep_debt);
        } else if (app->st.recovering) {
            snprintf(app->toast, sizeof(app->toast), "昨儿晕倒过，今天只剩半天");
        }
        app->scr = FB_SCR_HOME;
        app->sel = app->home_sel;
    }
}

void fb_app_init(fb_app_t *app, uint32_t seed) {
    memset(app, 0, sizeof(*app));
    app->seed = seed ? seed : 1u;
    fb_init(&app->st, 0, app->seed);
    app->scr = FB_SCR_INTRO;
    app->dirty = true;
}

void fb_app_mark_dirty(fb_app_t *app) { app->dirty = true; }
bool fb_app_need_render(const fb_app_t *app) { return app->dirty; }

void fb_app_on_key(fb_app_t *app, xm_input_t in) {
    if (in == XM_IN_NONE) return;
    app->dirty = true;

    const uint8_t count = page_item_count(app);
    if (in == XM_IN_UP_CLICK) {
        app->sel = (app->sel == 0) ? 0 : (uint8_t)(app->sel - 1);
        if (app->scr == FB_SCR_INTRO && !app->have_save) app->bg_sel = app->sel;
        return;
    }
    if (in == XM_IN_DOWN_CLICK) {
        if (count > 0 && app->sel + 1 < count) app->sel++;
        if (app->scr == FB_SCR_INTRO && !app->have_save) app->bg_sel = app->sel;
        return;
    }

    /* 长按返回：封面/结局不响应 */
    if (in == XM_IN_OK_LONG) {
        switch (app->scr) {
            case FB_SCR_LINES: app->scr = FB_SCR_HOME; app->sel = app->home_sel; break;
            case FB_SCR_ACTION: app->scr = FB_SCR_LINES; app->sel = app->line_sel; break;
            case FB_SCR_FINANCE:
            case FB_SCR_STORAGE:
            case FB_SCR_ABOUT: app->scr = FB_SCR_HOME; app->sel = app->home_sel; break;
            default: break;
        }
        return;
    }

    if (in != XM_IN_OK_CLICK) return;

    switch (app->scr) {
        case FB_SCR_INTRO:
            /* 有存档且停在第一项：直接接着上次的局，不重新 fb_init */
            if (app->have_save && app->sel == 0) {
                app->scr = FB_SCR_HOME;
                app->sel = app->home_sel;
                app->toast[0] = '\0';
                break;
            }
            /* 选了开局背景（有存档时是第 2~4 项）：旧档作废，重新开一局。
             * 置 save_dirty 让设备层立刻用新局覆盖旧档 —— 玩家选「重开」
             * 之后关一次机再打开，不能又回到那个他不想要的旧进度。 */
            uint8_t bg = app->have_save ? (uint8_t)(app->sel - 1) : app->sel;
            fb_init(&app->st, bg, app->seed);
            app->bg_sel = bg;
            app->have_save = false;
            app->save_dirty = true;
            app->scr = FB_SCR_HOME;
            app->sel = 0;
            break;

        case FB_SCR_HOME:
            app->home_sel = app->sel;   /* 记住光标，返回时不必从头找 */
            switch (app->sel) {
                case FB_MENU_WORK:
                    /* 干不动了就别让玩家白跑一趟：直接点明今天只能收工了 */
                    if (fb_hours_left(&app->st) == 0) {
                        snprintf(app->toast, sizeof(app->toast),
                                 "24 小时干满了，去收工睡觉");
                        break;
                    }
                    /* 记住上次去的线，一天连干几份不用每次从头翻 */
                    app->scr = FB_SCR_LINES;
                    app->sel = app->line_sel;
                    break;
                case FB_MENU_REST:
                    fb_rest(&app->st);
                    finish_day(app);
                    break;
                case FB_MENU_FINANCE: app->scr = FB_SCR_FINANCE; app->sel = 0; break;
                case FB_MENU_STORAGE: app->scr = FB_SCR_STORAGE; app->sel = 0; break;
                case FB_MENU_ENDDAY: finish_day(app); break;
                case FB_MENU_ABOUT: app->scr = FB_SCR_ABOUT; app->sel = 0; break;
                default: break;
            }
            break;

        case FB_SCR_LINES:
            if (fb_line_unlocked(&app->st, app->sel)) {
                app->line_sel = app->sel;
                app->scr = FB_SCR_ACTION;
                app->sel = 0;
            } else {
                snprintf(app->toast, sizeof(app->toast), "%s",
                         fb_unlock_hint(app->sel));
            }
            break;

        case FB_SCR_ACTION:
            switch (app->line_sel) {
                case FB_LINE_MARKET:
                    if (app->sel == 0) {
                        /* 换点位：只改视角，当日需求矩阵不重摇 */
                        size_t ns = 0;
                        (void)fb_spot_table(&ns);
                        uint8_t next = (uint8_t)((app->st.spot + 1) % ns);
                        fb_set_spot(&app->st, next);
                        snprintf(app->toast, sizeof(app->toast), "换到%s",
                                 fb_spot_name(next));
                    } else if (app->sel <= FB_KIND_COUNT) {
                        uint8_t kind = (uint8_t)(app->sel - 1);
                        if (!fb_buy_item(&app->st, kind, 1)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     app->st.inv_total >= app->st.storage
                                         ? "仓储满了，先扩容"
                                         : "现金不够进货");
                        }
                    } else if (can_run(app, FB_LINE_MARKET)) {
                        fb_run_market(&app->st, app->toast, sizeof(app->toast));
                        back_to_home_after_work(app);
                    }
                    break;

                case FB_LINE_DELIVERY:
                    if (!fb_line_unlocked(&app->st, FB_LINE_DELIVERY)) {
                        if (!fb_buy_ebike(&app->st)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     "现金不够买电动车");
                        }
                    } else if (can_run(app, FB_LINE_DELIVERY)) {
                        fb_run_delivery(&app->st, app->toast, sizeof(app->toast));
                        back_to_home_after_work(app);
                    }
                    break;

                case FB_LINE_GIG:
                    if (can_run(app, FB_LINE_GIG)) {
                        fb_run_gig(&app->st, app->sel, app->toast,
                                   sizeof(app->toast));
                        back_to_home_after_work(app);
                    }
                    break;

                case FB_LINE_RIDE:
                    if (!fb_line_unlocked(&app->st, FB_LINE_RIDE)) {
                        if (!fb_rent_car(&app->st)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     "押金 ¥8000 不够，先去借钱");
                        }
                    } else if (can_run(app, FB_LINE_RIDE)) {
                        fb_run_ride(&app->st, app->toast, sizeof(app->toast));
                        back_to_home_after_work(app);
                    }
                    break;

                case FB_LINE_LIVE:
                    if (!fb_line_unlocked(&app->st, FB_LINE_LIVE)) {
                        if (!fb_buy_live_gear(&app->st)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     "现金不够买设备");
                        }
                    } else if (can_run(app, FB_LINE_LIVE)) {
                        fb_run_live(&app->st, app->sel, app->toast,
                                    sizeof(app->toast));
                        back_to_home_after_work(app);
                    }
                    break;

                default: break;
            }
            break;

        case FB_SCR_FINANCE:
            switch (app->sel) {
                case 0:
                    if (fb_cashout(&app->st, 2000))
                        snprintf(app->toast, sizeof(app->toast),
                                 "刷卡到账 1940，欠款 +2000");
                    break;
                case 1:
                    if (fb_loan(&app->st, 1000))
                        snprintf(app->toast, sizeof(app->toast),
                                 "网贷到账 1000，欠款 +1000");
                    break;
                case 2:
                    if (fb_repay(&app->st, 500))
                        snprintf(app->toast, sizeof(app->toast), "还款 500");
                    else
                        snprintf(app->toast, sizeof(app->toast),
                                 "没钱或没债可还");
                    break;
                case 3:
                    if (fb_bank_deposit(&app->st, 1000))
                        snprintf(app->toast, sizeof(app->toast), "存 1000 日息1%%");
                    else
                        snprintf(app->toast, sizeof(app->toast), "现金不够存");
                    break;
                case 4:
                    if (fb_bank_withdraw(&app->st, 1000))
                        snprintf(app->toast, sizeof(app->toast), "取出 1000");
                    else
                        snprintf(app->toast, sizeof(app->toast), "存款不够取");
                    break;
                default: break;
            }
            break;

        case FB_SCR_STORAGE:
            if (!fb_expand_storage(&app->st)) {
                snprintf(app->toast, sizeof(app->toast), "现金不够扩容");
            }
            break;

        case FB_SCR_REPORT: {
            const fb_event_t *e = fb_roll_event(&app->st);
            if (e) {
                app->ev = e;
                app->ev_choice = 0;
                app->scr = FB_SCR_EVENT;
                app->sel = 0;
            } else {
                advance_after_event(app);
            }
            break;
        }

        case FB_SCR_EVENT:
            if (app->ev) {
                char msg[FB_LOG_SIZE];
                msg[0] = '\0';
                fb_apply_choice(&app->st, app->ev, app->ev_choice, msg,
                                sizeof(msg));
                if (msg[0]) {
                    snprintf(app->toast, sizeof(app->toast), "%s", msg);
                }
                app->ev = NULL;
            }
            advance_after_event(app);
            break;

        case FB_SCR_ENDING:
            /* 局已结束：重开时清掉存档，免得下次开机又翻出这具结束了的局。
             * save_clear 必须在 fb_app_init 之后置 —— init 会 memset 整个 app。 */
            app->seed = app->seed * 1664525u + 1013904223u;
            fb_app_init(app, app->seed);
            app->save_clear = true;
            break;

        case FB_SCR_ABOUT:
            app->scr = FB_SCR_HOME;
            app->sel = 0;
            break;

        default: break;
    }
}
