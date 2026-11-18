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

/* 把颜色按比例压暗。240x320 上做不出半透明层次（看不出来），
 * 只能用算好的实色 —— 未选中的行当色条就是 accent 压到 42%。 */
static uint32_t dim_color(uint32_t c, uint8_t pct) {
    if (pct > 100) pct = 100;
    const uint32_t r = ((c >> 16) & 0xFFu) * pct / 100u;
    const uint32_t g = ((c >> 8) & 0xFFu) * pct / 100u;
    const uint32_t b = (c & 0xFFu) * pct / 100u;
    return (r << 16) | (g << 8) | b;
}

/* 五条活路各一个色。小屏上颜色比文字先被认出来：行当页的左侧色条、
 * 场景角标、日报标题共用同一套，玩家靠颜色就知道自己在哪条线。 */
static uint32_t line_color(uint8_t line) {
    switch (line) {
        case FB_LINE_MARKET:   return 0xE07A3Fu;   /* 摆摊 · 橙 */
        case FB_LINE_DELIVERY: return 0x4FA88Bu;   /* 外卖 · 青绿 */
        case FB_LINE_GIG:      return 0xD9A441u;   /* 日结 · 琥珀 */
        case FB_LINE_RIDE:     return 0x7B7FD4u;   /* 网约车 · 紫蓝 */
        case FB_LINE_LIVE:     return 0xD45C7Fu;   /* 直播 · 粉 */
        default:               return FB_COLOR_ACCENT;
    }
}

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

/* 顶部场景横幅。整幅直接指向 Flash，不做解码也不做缩放。
 * y 由调用方给：封面没有状态条，图要从 0 开始铺满，其他页压在状态条下面。 */
static void draw_scene_at(xm_view_t *v, fb_scene_id_t id, int y) {
    if (id >= FB_SC_COUNT) return;
    fb_scene_t sc = scene_of(id);
    v->image = sc.data;
    v->image_x = 0;
    v->image_y = (int16_t)y;
    v->image_w = sc.w;
    v->image_h = sc.h;
    v->image_id = (uint8_t)id;
    /* 图下沿压一条深线，把图和内容区分开，避免文字像浮在画里 */
    v_rect(v, 0, y + (int)sc.h, 240, 2, 0x090C12u, true);
}

static void draw_scene(xm_view_t *v, fb_scene_id_t id) {
    draw_scene_at(v, id, FB_SCENE_Y);
}

/* 状态条：图标 + 日期。右上角电量由 xm_status 负责，这里只用到 x=176。 */
static void draw_status(fb_app_t *app, xm_view_t *v, fb_icon_id_t icon,
                        const char *text) {
    /* 标题条下沿一条细线。240x320 上加粗字体会糊，一条线就能把
     * 「标题区 / 场景区」分开，而且不占任何高度。 */
    v_rect(v, 0, FB_STATUS_H - 1, 240, 1, 0x2A3040u, true);
    v_icon(v, icon, 6, 5);
    v_line(v, 26, 4, 150, 16, XM_COLOR_INK, XM_ALIGN_LEFT,
           pool_raw(app, text));
}

/* 卡片：硬边框 + 选中底块 + 左侧色条 + 可选右侧数据点。
 * 选中态不用箭头符号（思源黑体对 ▲▼ 覆盖不稳），改用块面变化。
 * accent 让每个行当有自己的颜色；right 是右对齐的数据点（耗时/价格），
 * 让玩家在按 OK 之前就看到代价，而不是按下去才被告诉"你还缺 2800"。 */
static void draw_card_ex(fb_app_t *app, xm_view_t *v, int x, int y, int w, int h,
                         bool selected, bool enabled, fb_icon_id_t icon,
                         uint32_t accent, const char *text,
                         const char *right, uint32_t right_color) {
    const bool hi = selected && enabled;
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h, hi ? FB_COLOR_SELBG : FB_COLOR_CARD,
           true);
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h, hi ? accent : XM_COLOR_LINE, false);
    /* 左侧色条不论选中与否都占位：光标上下移动时卡片内容不会左右跳。
     * 未选中压到 70% 而不是更暗 —— 42% 时五条线的颜色几乎分辨不出来，
     * 等于白配了一套色。 */
    v_rect(v, x, y, 3, (uint16_t)h, hi ? accent : dim_color(accent, 70), true);

    int tx = x + 9;
    if (icon > FB_ICO_NONE) {
        v_icon(v, icon, tx, y + (h - (int)FB_ICON_SIZE) / 2);
        tx += (int)FB_ICON_SIZE + 6;
    }
    const uint32_t color = !enabled ? XM_COLOR_DISABLE : (hi ? accent : XM_COLOR_INK);
    /* 展开态（h >= 32）文字靠上，把下半张卡让给调用方画描述；
     * 普通高度居中。这样展开/收起时文字不会上下跳太多。 */
    const int ty = (h >= 32) ? y + 3 : y + (h - 16) / 2;
    int tw = x + w - tx - 6;
    if (right) {
        v_line(v, x + w - 58, ty, 52, 16, right_color, XM_ALIGN_RIGHT, right);
        tw -= 56;
    }
    if (tw > 10) {
        v_line(v, tx, ty, (uint16_t)tw, 16, color, XM_ALIGN_LEFT,
               pool_raw(app, text));
    }
}

static void draw_card(fb_app_t *app, xm_view_t *v, int x, int y, int w, int h,
                      bool selected, bool enabled, fb_icon_id_t icon,
                      const char *text) {
    draw_card_ex(app, v, x, y, w, h, selected, enabled, icon, FB_COLOR_ACCENT,
                 text, NULL, 0);
}

/* 犯困时占掉底部提示行。
 *
 * 瞌睡会把收入打到八折（撑不住时六折），这件事必须在按 OK 之前就看见 ——
 * 原先它藏在场景图右下角那个小角标的第二行里，字只有 12px，
 * 玩家干着活也未必发现自己在白干。提示行本来就是"按下去会发生什么"，
 * 让警告顶掉键位说明是划算的：走到这一步的人早就会用上下键了。 */
static const char *hint_or_drowsy(const fb_app_t *app, const char *normal) {
    if (app->st.drowsy == FB_DROWSY_CRITICAL) return "撑不住了 · 收入六折，快收工";
    if (app->st.drowsy == FB_DROWSY_TIRED) return "犯困 · 收入八折，早点收工";
    return normal;
}

static void draw_hint(fb_app_t *app, xm_view_t *v, const char *hint) {
    /* 提示行上方一条细线：把「这是按键说明，不是正文」标出来，
     * 否则底部这行和上面的数据在视觉上是同一层级 */
    v_rect(v, 12, 296, 216, 1, 0x2A3040u, true);
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
        /* 按行宽硬切会把词劈开 —— 「因为希望这东西我买不起」被切成
         * 「…希望这东」/「西我买不起」，读起来是断的。中文没有词边界可断，
         * 但句子里有标点：宁可这一行短一点，也要断在标点后面。
         * 只在断点落在半行之后时才回退，避免制造两三个字的孤行。 */
        if (*p && count >= chars_per_line) {
            static const char *k_marks[] = {"，", "。", "！", "？", "；", "：", "、", "…"};
            size_t best = 0;
            for (size_t m = 0; m < sizeof(k_marks) / sizeof(k_marks[0]); ++m) {
                const char *q = buf;
                size_t ml = strlen(k_marks[m]);
                while ((q = strstr(q, k_marks[m])) != NULL) {
                    size_t off = (size_t)(q - buf) + ml;
                    if (off > best) best = off;
                    q += ml;
                }
            }
            if (best > 0 && best * 2 >= n) {
                p -= (n - best);
                count = 0;
                for (size_t i = 0; i < best;) {
                    uint8_t len = xm_utf8_char_len((uint8_t)buf[i]);
                    i += (len ? len : 1);
                    ++count;
                }
                n = best;
                buf[n] = '\0';
            }
        }
        /* 宽度必须跟着 x 走。原先写死 224，一给左边留缩进（x=30）
         * 就直接把文字排到屏幕外 —— 门禁的横向越界检查正是拦这个的。 */
        v_line(v, x, y + rows * line_h, (uint16_t)(240 - x - 8), size, color,
               XM_ALIGN_LEFT, pool_put(app, "%s", buf));
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

/* 六个入口各配一个色：挣钱暖橙、休息青、借钱琥珀、囤货紫、
 * 收工红、说明灰。纯文字网格在小屏上六个格子长得一模一样，
 * 加了色条之后靠颜色就能定位，不用逐格读字。 */
static uint32_t menu_color(uint8_t idx) {
    switch (idx) {
        case FB_MENU_WORK: return FB_COLOR_ACCENT;
        case FB_MENU_REST: return 0x4FA88Bu;
        case FB_MENU_FINANCE: return FB_COLOR_WARN;
        case FB_MENU_STORAGE: return 0x7B7FD4u;
        case FB_MENU_ENDDAY: return 0xC4443Cu;
        default: return 0x6C7484u;
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

/* 三个开局背景各一个图标和色：被裁员工是关上的门（红）、
 * 攒下一点钱是现金（青）、卡还能刷是信用卡（琥珀）。
 * 同一个现金图标画三遍，玩家只能逐字读才知道选的是哪个开局。 */
static fb_icon_id_t bg_icon(uint8_t i) {
    switch (i) {
        case 0: return FB_ICO_DOOR;
        case 1: return FB_ICO_CASH;
        default: return FB_ICO_CARD;
    }
}

static uint32_t bg_color(uint8_t i) {
    switch (i) {
        case 0: return 0xC4443Cu;
        case 1: return 0x4FA88Bu;
        default: return FB_COLOR_WARN;
    }
}

static void render_intro(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_bg_t *bgs = fb_bg_table(&n);
    /* 封面没有状态条，图从 y=0 铺满 240x140 —— 那 140px 里最亮的是窗外那片
     * 城市，它就是这一局要抢的东西。旧版图只占 104 高、上面还空着 26px 黑边，
     * 屏幕四成都浪费掉了。 */
    draw_scene_at(v, FB_SC_INTRO, 0);

    /* 标题压在场景图下沿的深色带上，而不是浮在纯黑背景里 ——
     * 标题 24px、副标题 12px、下面一条短装饰线，三件套把封面顶成一个招牌。 */
    v_line(v, 8, 146, 224, 24, XM_COLOR_INK, XM_ALIGN_CENTER,
           pool_raw(app, "折叠北京"));
    v_line(v, 8, 172, 224, 12, FB_COLOR_ACCENT, XM_ALIGN_CENTER,
           pool_raw(app, "一百天 · 活下去"));
    v_rect(v, 104, 188, 32, 2, FB_COLOR_ACCENT, true);

    /* 有存档时多出「继续」一项，四项只能压到 25 行距（23 高）。 */
    if (app->have_save) {
        draw_card_ex(app, v, 6, 192, 228, 26, app->sel == 0, true, FB_ICO_DOOR,
                     FB_COLOR_ACCENT,
                     pool_put(app, "继续上次：第 %d 天", (int)app->save_day),
                     "续档", XM_COLOR_SUBINK);
        for (uint8_t i = 0; i < (uint8_t)n && i < 3; ++i) {
            draw_card_ex(app, v, 6, 221 + i * 25, 228, 23, app->sel == i + 1,
                         true, bg_icon(i), bg_color(i), pool_raw(app, bgs[i].name),
                         pool_put(app, "¥%d", (int)bgs[i].cash), XM_COLOR_SUBINK);
        }
    } else {
        for (uint8_t i = 0; i < (uint8_t)n && i < 3; ++i) {
            draw_card_ex(app, v, 6, 196 + i * 32, 228, 30, app->bg_sel == i, true,
                         bg_icon(i), bg_color(i), pool_raw(app, bgs[i].name),
                         pool_put(app, "¥%d", (int)bgs[i].cash), XM_COLOR_SUBINK);
        }
    }
    draw_hint(app, v, app->have_save ? "↑↓ 选    OK 确定" : "↑↓ 选开局    OK 开始");
}

/* 24 小时条：把「今天已经花掉多少小时」画成一条进度。
 *
 * 之前这一页把工时和睡眠压成两行 12px 小字，糊在场景图右下角 ——
 * 而「还剩几小时」是这一局最核心的资源，它必须一眼看得出来。
 * 一根 240x8 的条：橙色是干掉的，暗色是还能睡的，
 * x=160 那道刻度是 8 小时睡眠线，越过它再干，今晚就睡不够。 */
#define FB_DAYBAR_Y (FB_CONTENT_Y - 8)   /* 贴在画面最下沿，刚好接上内容区 */

static void draw_day_bar(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    const int y = FB_DAYBAR_Y;
    int used = st->hours_used;
    if (used > FB_DAY_HOURS) used = FB_DAY_HOURS;
    v_rect(v, 0, y, 240, 8, FB_COLOR_BAR_BG, true);
    int w = 240 * used / FB_DAY_HOURS;
    if (w > 0) {
        v_rect(v, 0, y, (uint16_t)w, 8,
               used >= 20 ? XM_COLOR_RED : FB_COLOR_ACCENT, true);
    }
    /* 8 小时睡眠线：刻度比条高 1px，压在两色交界上方才看得见。
     * 一小时都没干的时候不画 —— 那时光标线上什么都没有，孤零零一道白痕
     * 只会被当成渲染脏点。 */
    if (used > 0) {
        v_rect(v, 240 * (FB_DAY_HOURS - FB_SLEEP_OK) / FB_DAY_HOURS, y - 1, 1, 10,
               0xEDE6D8u, true);
    }
}

/* 主面板底部的一格入口：图标在上、文字在下。
 *
 * 六个入口原先排成 2 列 3 行的等大方块，六张卡长得一模一样，
 * 看上去像一张设置菜单 —— 玩家找不到该按哪个。现在只留一个
 * 「去干活」大按钮当主行动，其余五个压成图标格，主次立刻分明；
 * 而且三键只有上/下，一横排的宫格和下移一格的走向是一致的。 */
static void draw_dock(fb_app_t *app, xm_view_t *v, int x, int y, int w, int h,
                      bool selected, fb_icon_id_t icon, uint32_t accent,
                      const char *label) {
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h, selected ? FB_COLOR_SELBG : FB_COLOR_CARD, true);
    v_rect(v, x, y, (uint16_t)w, (uint16_t)h, selected ? accent : XM_COLOR_LINE, false);
    /* 顶部 3px 色条：未选中也保留，靠颜色定位入口，不靠读字 */
    v_rect(v, x, y, (uint16_t)w, 3, selected ? accent : dim_color(accent, 60), true);
    /* 图标居中偏上、文字贴底：40 高的格子里 16+12 只占 28，必须让两者
     * 之间留出空隙，否则 16px 字实际渲染高度会压到图标下沿，
     * 看起来像糊在一起。 */
    v_icon(v, icon, x + (w - (int)FB_ICON_SIZE) / 2, y + 5);
    v_line(v, x, y + h - 16, (uint16_t)w, 12,
           selected ? accent : XM_COLOR_SUBINK, XM_ALIGN_CENTER,
           pool_raw(app, label));
}

static void render_home(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_status(app, v, FB_ICO_CAL, pool_put(app, "第 %d 天 / 100", st->day));
    draw_scene(v, FB_SC_HOME);

    /* 场景图不再被任何东西压住。原先新闻条压在图顶 18px、工时段压在图右下
     * 104x30，一张 240x104 的图只剩中间一条 240x56 的缝 —— 那是这一页
     * 「看着不吸引人」的真正原因：观众从来没见过完整的那张画。 */
    draw_day_bar(app, v);

    /* 今日新闻：北京的天和行情每天都要交代，单独占一条，不叠在画上 */
    const int news_y = FB_CONTENT_Y + 2;
    v_rect(v, 0, news_y, 240, 18, 0x0B0E14u, true);
    v_line(v, 6, news_y + 3, 228, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_clip(app, fb_news_today(st), 18));

    /* 现金：这一页的主数字。¥ 与 24px 数字底部对齐，数字用暖白，
     * 快到零时转琥珀 —— 钱少到一定程度，颜色比数字更早被注意到。 */
    v_icon(v, FB_ICO_CASH, 8, news_y + 24);
    /* ¥ 的框必须收在数字左边界之前（x=40）：24px 数字的框是 158..182，
     * 16px 的 ¥ 如果给到 x=28 w=14，两个 label 会在 x=40..42 上重叠 ——
     * 布局门禁就是靠这个判出"两行压字"的。 */
    v_line(v, 26, news_y + 30, 12, 16, XM_COLOR_SUBINK, XM_ALIGN_LEFT, "¥");
    v_line(v, 40, news_y + 22, 82, 24, st->cash < 100 ? FB_COLOR_WARN : XM_COLOR_INK,
           XM_ALIGN_LEFT, pool_put(app, "%d", (int)st->cash));

    /* 健康 / 精力：图标 + 条 + 数值。20% 处一道深色刻度，
     * 掉到线下就该歇了 —— 纯数字给不出紧迫感。 */
    for (int i = 0; i < 2; ++i) {
        const int ry = news_y + 24 + i * 18;
        const int val = i == 0 ? st->health : (int)st->stamina;
        uint32_t col;
        if (i == 0) col = st->health < 30 ? XM_COLOR_RED : XM_COLOR_BLUE;
        else col = st->stamina < 20 ? FB_COLOR_WARN : FB_COLOR_ACCENT;
        v_icon(v, i == 0 ? FB_ICO_HEART : FB_ICO_BOLT, 128, ry);
        draw_bar(v, 148, ry + 3, 56, 7, val, col);
        v_rect(v, 148 + 56 * 20 / 100, ry + 3, 1, 7, 0x0B0E14u, true);
        v_line(v, 204, ry, 28, 12, XM_COLOR_SUBINK, XM_ALIGN_RIGHT,
               pool_put(app, "%d", val));
    }

    /* 一行账目：欠债、存款、干了几小时、还能睡几小时、现在困不困。
     * 全是硬信息，但不该抢现金的位置，所以压成一条 12px。 */
    const int sub_y = news_y + 58;
    v_line(v, 8, sub_y, 224, 12,
           (st->debt > 0 || st->sleep_debt >= 4) ? FB_COLOR_WARN : XM_COLOR_SUBINK,
           XM_ALIGN_LEFT,
           pool_put(app, "债%d·存%d·干%u时·睡%u时·%s", (int)st->debt,
                    (int)st->savings, (unsigned)st->hours_used,
                    (unsigned)fb_sleep_hours(st), fb_drowsy_label(st->drowsy)));

    /* 主行动：整块实心橙。选中时换成最亮的一档，未选中压到 32% ——
     * 它任何时候都是这一屏最亮的东西，但不会盖掉选中反馈。 */
    const bool work_sel = app->sel == FB_MENU_WORK;
    v_rect(v, 6, 214, 228, 34,
           work_sel ? FB_COLOR_ACCENT : dim_color(FB_COLOR_ACCENT, 32), true);
    v_line(v, 22, 221, 140, 20,
           work_sel ? 0x1A1008u : FB_COLOR_ACCENT, XM_ALIGN_LEFT,
           pool_raw(app, "去干活"));
    v_line(v, 166, 226, 62, 12,
           work_sel ? 0x4A2A10u : dim_color(FB_COLOR_ACCENT, 75), XM_ALIGN_RIGHT,
           pool_put(app, "还剩 %u 时", (unsigned)fb_hours_left(st)));

    /* 其余五个入口排成一横排图标格 */
    for (uint8_t i = 1; i < FB_MENU_COUNT; ++i) {
        draw_dock(app, v, 6 + (int)(i - 1) * 46, 252, 44, 40, app->sel == i,
                  menu_icon(i), menu_color(i), menu_label(i));
    }

    /* 底部这一行优先给「上一次干活的结果」：一天能连打几份工，
     * 玩家必须看到上一单挣了多少、扣了什么，否则连干三份是在盲赌。 */
    draw_hint(app, v, app->toast[0] ? app->toast : "↑↓ 选择    OK 确定");
}

static void render_lines(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_line_meta_t *meta = fb_line_table(&n);
    draw_status(app, v, FB_ICO_WORK, "去哪儿挣钱");
    draw_scene(v, FB_SC_HOME);
    draw_day_bar(app, v);

    /* 五条线全展开需要 5x40=200，152 的内容区放不下，所以选中的展开成两行、
     * 其余压到 26。四条 28 + 一条 44 = 156，落在 134..290。
     *
     * 关键改动：没装备的三条线**也是可选、可高亮的**。原先把 unlocked 当
     * enabled 传给 draw_card，整张卡画成禁用灰，选中时橙色高亮也不出现 ——
     * 光标明明停在上面，屏幕却像是不让选，玩家只能以为这功能坏了。
     * 这里的语义是「待决策」而不是「不可用」：选中就告诉你缺多少钱、按 OK 去置办。 */
    int y = 134;
    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        const bool unlocked = fb_line_unlocked(&app->st, i);
        const bool sel = app->sel == i;
        const int h = sel ? 42 : 26;
        const uint32_t accent = line_color(i);

        /* 右侧数据点：解锁了看这活要几小时，没解锁看要掏多少钱。
         * 网约车两处都要特殊对待：
         *   没解锁时那 8000 是押金不是车价，写「押」；
         *   解锁后它不是一劳永逸的资产 —— 月租到期交不出就来收车，
         *   所以右侧换成剩余天数，让倒计时一直在玩家眼前。 */
        const int16_t ride_left = (i == FB_LINE_RIDE) ? fb_ride_days_left(&app->st) : 0;
        const char *right;
        uint32_t right_color = FB_COLOR_WARN;
        if (!unlocked) {
            right = (i == FB_LINE_RIDE) ? pool_put(app, "押%d", (int)meta[i].startup)
                                        : pool_put(app, "¥%d", (int)meta[i].startup);
        } else if (i == FB_LINE_RIDE) {
            right = ride_left > 0 ? pool_put(app, "剩%d天", (int)ride_left) : "到期";
            right_color = ride_left <= 3 ? FB_COLOR_WARN : XM_COLOR_SUBINK;
        } else {
            right = pool_put(app, "%uh", (unsigned)fb_line_hours(i));
            right_color = XM_COLOR_SUBINK;
        }

        draw_card_ex(app, v, 6, y, 228, h, sel, true, line_icon(i), accent,
                     pool_raw(app, meta[i].name), right, right_color);

        if (sel) {
            if (unlocked) {
                /* 车已经租下来的人不需要再读「押金高常要借钱」，
                 * 他需要知道还剩几天、到期要交多少。 */
                const bool ride_urgent = i == FB_LINE_RIDE && ride_left <= 3;
                const char *d = meta[i].desc;
                if (i == FB_LINE_RIDE) {
                    d = ride_urgent
                            ? pool_put(app, "还剩 %d 天，交不出车就被收走", (int)ride_left)
                            : pool_put(app, "还剩 %d 天，到期交 2800", (int)ride_left);
                }
                v_line(v, 33, y + 21, 195, 12,
                       ride_urgent ? FB_COLOR_WARN : XM_COLOR_SUBINK, XM_ALIGN_LEFT,
                       d);
            } else {
                const int32_t gap = meta[i].startup - app->st.cash;
                v_line(v, 33, y + 21, 195, 12,
                       gap > 0 ? FB_COLOR_WARN : XM_COLOR_BLUE, XM_ALIGN_LEFT,
                       gap > 0 ? pool_put(app, "还缺 %d，按 OK 去借钱", (int)gap)
                               : pool_put(app, "钱够了，按 OK 直接置办"));
            }
        }
        y += h + 2;
    }

    /* 底部提示跟着光标走：光标停在哪条线，就告诉玩家这一步按下去会发生什么 */
    const fb_line_meta_t *cur = &meta[app->sel < n ? app->sel : 0];
    const char *hint = "↑↓ 选行当    OK 开工";
    if (!fb_line_unlocked(&app->st, app->sel < n ? app->sel : 0)) {
        hint = (cur->startup > app->st.cash) ? "OK 去借钱置办装备" : "OK 置办装备并开工";
    }
    draw_hint(app, v, hint_or_drowsy(app, hint));
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
    draw_day_bar(app, v);

    /* 7 项要塞进 134..284：每项 21 行距（20 高），出摊卡 24 高。
     * 早先 24 行距 + 5 货 + 出摊 = 150，加上点位行就压到提示行了。 */
    /* ×号必须写明乘的是客流（今天最多能卖几份），不是进价/卖价。
     * 玩家把「夜市 ×130%」读成「这里卖得贵」是合理误解 ——
     * 八个点位的进价卖价其实一模一样，货品行才写着价格。 */
    draw_card(app, v, 6, 134, 228, 20, app->sel == 0, true, FB_ICO_CAL,
              pool_put(app, "点位 %s 客流×%u%%", spots[spot].name,
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
    draw_hint(app, v, hint_or_drowsy(app, "OK 换点位/进货/出摊"));
}

static void render_gig(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_gig_t *gigs = fb_gig_table(&n);
    draw_status(app, v, FB_ICO_BOLT, "日结零工");
    draw_scene(v, FB_SC_GIG);
    draw_day_bar(app, v);

    /* 6 个工种要塞进 134..286 这 152px：每项 23 高、行距 25，名称/工资/体力压成一行。
     * 早先按 42 行距排（每项占标题+描述两行），最后两项直接排到了屏幕外面。 */
    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        int y = 134 + i * 25;
        /* 左栏工种+工资，右栏精力。原先每行前面挂一个闪电图标、后面接
         * 「· 精力28」，六行下来文字顶到卡片右边缘，图标又行行重复、
         * 一点信息量都没有 —— 名字和数字分栏比加图标清楚得多。 */
        draw_card_ex(app, v, 6, y, 228, 23, app->sel == i, true, FB_ICO_NONE,
                     FB_COLOR_ACCENT,
                     pool_put(app, "%s  ¥%d", gigs[i].name, (int)gigs[i].wage),
                     pool_put(app, "精力 %u", gigs[i].stamina), XM_COLOR_SUBINK);
    }
    draw_hint(app, v, hint_or_drowsy(app, "↑↓ 选工种    OK 上工"));
}

static void render_topic(fb_app_t *app, xm_view_t *v) {
    size_t n = 0;
    const fb_topic_t *tp = fb_topic_table(&n);
    draw_status(app, v, FB_ICO_STAR, pool_put(app, "粉丝 %u", app->st.fans));
    draw_scene(v, FB_SC_LIVE);
    draw_day_bar(app, v);

    /* 原先标题 16px 排在 y+3、描述 12px 排在 y+19，行间距只有 16px，
     * 而 16px 字实际渲染高度超过字号本身 —— 两行叠在一起糊成一团。
     * 改成和行当页同一套结构：一行标题 + 右侧数据点，只有选中的才展开描述。 */
    int y = 134;
    for (uint8_t i = 0; i < (uint8_t)n; ++i) {
        const bool sel = app->sel == i;
        const int h = sel ? 40 : 26;
        draw_card_ex(app, v, 6, y, 228, h, sel, true, FB_ICO_STAR, 0xD45C7Fu,
                     pool_raw(app, tp[i].name),
                     pool_put(app, "涨粉%d", tp[i].fans),
                     sel ? FB_COLOR_ACCENT : XM_COLOR_SUBINK);
        if (sel) {
            v_line(v, 33, y + 22, 195, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
                   pool_put(app, "%s · 风险%d%%", tp[i].note, tp[i].risk_pct));
        }
        y += h + 2;
    }
    /* 连播加成是这条线唯一需要「坚持」的机制，必须让玩家看见
     * 自己已经播了几天、再加成到多少 —— 看不见的加成等于没有。 */
    const uint8_t bonus = fb_live_bonus_pct(&app->st);
    v_line(v, 8, 266, 224, 12, bonus > 100 ? FB_COLOR_ACCENT : XM_COLOR_SUBINK,
           XM_ALIGN_LEFT,
           pool_put(app, "连播 %u 天 · 涨粉 ×%u.%u", (unsigned)app->st.live_streak,
                    bonus / 100, (bonus % 100) / 10));
    draw_hint(app, v, hint_or_drowsy(app, "↑↓ 选题    OK 开播"));
}

/* 外卖 / 网约车：装备 + 跑 */
static void render_vehicle(fb_app_t *app, xm_view_t *v, uint8_t line) {
    const fb_state_t *st = &app->st;
    bool unlocked = fb_line_unlocked(st, line);
    fb_icon_id_t ic = line == FB_LINE_DELIVERY ? FB_ICO_BOX : FB_ICO_CARD;
    draw_status(app, v, ic, line == FB_LINE_DELIVERY ? "送外卖" : "网约车");
    draw_scene(v, line == FB_LINE_DELIVERY ? FB_SC_DELIVERY : FB_SC_RIDE);
    draw_day_bar(app, v);

    if (!unlocked) {
        /* 价格取引擎的行当元数据，不在这里另抄一份数字 ——
         * 抄一份就意味着改价时两边会不同步 */
        size_t ln = 0;
        const fb_line_meta_t *lm = fb_line_table(&ln);
        const int32_t price = (line < ln) ? lm[line].startup : 0;

        draw_para(app, v, 8, 140,
                  line == FB_LINE_DELIVERY
                      ? "先置办电动车和装备，才能接单。"
                      : "先交押金租辆车，才能跑活。",
                  FB_BODY_W, 22, 16, XM_COLOR_INK, 3);
        draw_card_ex(app, v, 6, 212, 228, 30, app->sel == 0, true, ic,
                     line_color(line), "置办装备开工",
                     pool_put(app, "¥%d", (int)price), FB_COLOR_WARN);
        if (st->cash < price) {
            v_line(v, 8, 248, 224, 12, FB_COLOR_WARN, XM_ALIGN_LEFT,
                   pool_put(app, "还差 %d，按 OK 去借钱", (int)(price - st->cash)));
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
    draw_hint(app, v, hint_or_drowsy(app, "OK 确定    长按返回"));
}

/* 财务页：借钱 / 还债 / 存钱。5 张卡按 31 行距排在 140..291，
 * 与底部提示行（300）留 9px，再紧就会糊成一片。 */
static void render_finance(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_status(app, v, FB_ICO_CARD,
                pool_put(app, "现金 %d · 存 %d", (int)st->cash, (int)st->savings));
    draw_scene(v, FB_SC_REPORT);

    /* 右侧标金额：进钱走青、出钱走琥珀。文字里不再重复数字，
     * 眼睛扫右列就能比代价，不用逐条读句子。 */
    draw_card_ex(app, v, 6, 140, 228, 27, app->sel == 0, true, FB_ICO_CARD,
                 FB_COLOR_WARN, "刷卡套现 费3%", "+1940", XM_COLOR_BLUE);
    draw_card_ex(app, v, 6, 171, 228, 27, app->sel == 1, true, FB_ICO_CASH,
                 FB_COLOR_WARN, "网贷 日息1%", "+1000", XM_COLOR_BLUE);
    draw_card_ex(app, v, 6, 202, 228, 27, app->sel == 2, st->debt > 0,
                 FB_ICO_CASH, 0x4FA88Bu, "还 500 欠款",
                 pool_put(app, "欠%d", (int)st->debt), FB_COLOR_WARN);
    draw_card_ex(app, v, 6, 233, 228, 27, app->sel == 3, st->cash > 0,
                 FB_ICO_BOX, 0x4FA88Bu, "存 1000 进银行", "+1%", XM_COLOR_BLUE);
    draw_card_ex(app, v, 6, 264, 228, 27, app->sel == 4, st->savings > 0,
                 FB_ICO_BOX, 0x4FA88Bu, "取 1000 出来", "-1000", FB_COLOR_WARN);
    /* 从行当页被引到这里时底部会带一句「还缺 N 元」，玩家知道该按哪一项 */
    draw_hint(app, v, app->toast[0] ? app->toast : "↑↓ 选择    OK 确定");
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
    /* 「健康 95 · 精神」原来读起来像少了个数 —— 瞌睡标签只有真犯困时才有意义，
     * 平时别挂在健康后面冒充字段。 */
    const char *drowsy_note = st->drowsy == FB_DROWSY_CRITICAL ? " · 撑不住"
                            : st->drowsy == FB_DROWSY_TIRED ? " · 犯困" : "";
    v_line(v, 8, 204, 224, 12,
           st->sleep_debt >= 4 ? XM_COLOR_RED
                               : (st->sleep_debt ? FB_COLOR_WARN : XM_COLOR_SUBINK),
           XM_ALIGN_LEFT,
           pool_put(app, "欠觉 %u 天 · 健康 %d%s", (unsigned)st->sleep_debt,
                    st->health, drowsy_note));

    /* 当日语录：亏了给丧的陪着丧，挣了给毒鸡汤扎一刀。这是《活着》的底色。 */
    int32_t net = st->day_earned - st->day_spent;
    uint8_t qk = fb_quote_daily_kind(st->day, 1, net);
    draw_para(app, v, 8, 222, fb_quote_daily(st->day, 1, net), FB_SMALL_W, 15, 12,
              qk == FB_QUOTE_SANG ? XM_COLOR_SUBINK : FB_COLOR_ACCENT, 2);

    /* 明细（吃饭/交通/房租/车租/健康……）给两行。一行 17 个字宽装不下
     * 一天十来段流水，原来车租和健康全被切在屏外 —— 玩家只能看到钱少了，
     * 看不到为什么少。两行刚好落在语录(到 252)与底部提示线(296)之间。
     * 丢车那天整段标红：那是这一局最贵的一次意外。 */
    draw_para(app, v, 8, 258, app->toast, FB_SMALL_W, 15, 12,
              app->car_lost ? XM_COLOR_RED : XM_COLOR_INK, 2);
    draw_hint(app, v, "OK 继续");
}

static void render_event(fb_app_t *app, xm_view_t *v) {
    const fb_event_t *e = app->ev;
    if (!e) return;
    draw_status(app, v, FB_ICO_INFO, e->title);
    draw_scene(v, FB_SC_EVENT);

    draw_para(app, v, 8, 140, e->text, FB_BODY_W, 22, 16, XM_COLOR_INK, 3);

    /* 选项按 choice_n 全画出来，不再固定 2 张。
     * 「城管来了」本来就是三条（认罚 / 收摊跑路 / 托熟人），第三条原先
     * 在屏上根本不存在，玩家不知道还有别的活路。项数越多卡片越矮：
     * 三张 26px 刚好塞进正文(到 206)与底部提示(296)之间。 */
    const uint8_t n = e->choice_n ? e->choice_n : 1;
    const int top = 210, bottom = 294;
    const int gap = (n >= 3) ? 3 : 6;
    int h = (bottom - top - gap * (n - 1)) / (int)n;
    if (h > 34) h = 34;
    for (uint8_t i = 0; i < n; ++i) {
        draw_card(app, v, 6, top + i * (h + gap), 228, h, app->sel == i, true,
                  FB_ICO_NONE, e->choices[i].label);
    }
    draw_hint(app, v, "↑↓ 选    OK 确定");
}

static void render_ending(fb_app_t *app, xm_view_t *v) {
    const fb_state_t *st = &app->st;
    draw_scene(v, FB_SC_ENDING);

    /* 标题 + 一条居中短线：把「结局」和下面那笔账分成两个视觉块。
     * 原先标题和正文之间没有分隔，一屏字全糊在一起。 */
    v_line(v, 8, 136, 224, 24, FB_COLOR_ACCENT, XM_ALIGN_CENTER,
           pool_raw(app, fb_end_title(st->ending)));
    v_rect(v, 108, 164, 24, 2, FB_COLOR_ACCENT, true);
    draw_para(app, v, 8, 174, fb_end_text(st->ending), FB_BODY_W, 22, 16,
              XM_COLOR_INK, 3);

    /* 净值单独装进一张卡 —— 这是玩家一百天唯一带得走的东西 */
    v_rect(v, 6, 232, 228, 40, FB_COLOR_CARD, true);
    v_rect(v, 6, 232, 228, 40, XM_COLOR_LINE, false);
    v_rect(v, 6, 232, 3, 40, FB_COLOR_ACCENT, true);
    v_icon(v, FB_ICO_CASH, 16, 244);
    v_line(v, 38, 234, 180, 24, XM_COLOR_INK, XM_ALIGN_LEFT,
           pool_put(app, "净值 %d", (int)fb_net_worth(st)));
    v_line(v, 16, 278, 208, 12, XM_COLOR_SUBINK, XM_ALIGN_LEFT,
           pool_put(app, "撑到第 %d 天 · 欠债 %d",
                    st->day > FB_TOTAL_DAYS ? FB_TOTAL_DAYS : st->day,
                    (int)st->debt));
    draw_hint(app, v, "OK 再来一局");
}

/* 说明页拆成三节。原先是一整块 9 行文字，240x320 上没人读得下去 ——
 * 分节之后每节两三行、标题带图标，扫一眼就能定位要找的规则。
 * 正文从 x=30 起排，可用宽度 202px，12px CJK 一行只能放 16 字，
 * 所以每节文案都按 16 字/行 断句，避免句号掉到行首。 */
static const char *k_help_1 = "摆摊·外卖·日结·网约车·直播";
static const char *k_help_2 = "干活就没得睡，睡不够掉健康，欠觉犯困掉钱，再撑晕倒。";
static const char *k_help_3 = "健康到零就结束。挑点位、存钱、连播都有讲究。";

static void draw_section(fb_app_t *app, xm_view_t *v, fb_icon_id_t icon,
                         uint32_t color, int y, const char *title) {
    v_icon(v, icon, 8, y + 2);
    v_line(v, 30, y, 200, 16, color, XM_ALIGN_LEFT, pool_raw(app, title));
    /* 标题下方一条细线，把"这一节到哪儿为止"标出来 */
    v_rect(v, 30, y + 20, 196, 1, 0x232A38u, true);
}

static void render_about(fb_app_t *app, xm_view_t *v) {
    draw_status(app, v, FB_ICO_INFO, "玩法说明");
    draw_scene(v, FB_SC_HOME);

    draw_section(app, v, FB_ICO_WORK, FB_COLOR_ACCENT, 136, "五条活路");
    draw_para(app, v, 30, 158, k_help_1, 16, 16, 12, XM_COLOR_SUBINK, 1);

    draw_section(app, v, FB_ICO_CLOCK, 0x4FA88Bu, 180, "24 小时怎么花");
    draw_para(app, v, 30, 202, k_help_2, 16, 17, 12, XM_COLOR_INK, 2);

    draw_section(app, v, FB_ICO_HEART, 0xD45C7Fu, 242, "别把命搭进去");
    draw_para(app, v, 30, 264, k_help_3, 16, 17, 12, XM_COLOR_INK, 2);

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
/* 缺装备又买不起：直接把玩家送到借钱页，并说清还差多少。
 * 原先只弹一句「现金不够买电动车」就把人丢回原页 —— 玩家得自己想到
 * 回主面板 → 借钱 → 再翻回行当页，中间任何一步都可能直接退出游戏。 */
static void goto_finance_for(fb_app_t *app, uint8_t line) {
    size_t n = 0;
    const fb_line_meta_t *meta = fb_line_table(&n);
    const int32_t need = (line < n) ? meta[line].startup : 0;
    int32_t gap = need - app->st.cash;
    if (gap < 0) gap = 0;
    snprintf(app->toast, sizeof(app->toast), "还缺 %d 元，先借点周转",
             (int)gap);
    app->scr = FB_SCR_FINANCE;
    app->sel = 0;                 /* 落在「刷卡套现」，按 OK 就能拿到钱 */
}

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
    const bool had_car = app->st.has_car != 0;
    fb_end_day(&app->st, app->toast, sizeof(app->toast));
    /* 收工页的两行明细未必容得下「车被收走」，次日首页再顶一条：
     * 玩家回到主面板时，网约车行当已经变成「缺 ¥8000」，不问一句
     * 就真成了凭空消失。 */
    if (had_car && !app->st.has_car) app->car_lost = true;
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
        if (app->car_lost) {
            /* 丢车是这一局最贵的一次意外（押金 8000 + 本月租金），
             * 优先于欠觉和晕倒，回首页第一眼就得知道。 */
            snprintf(app->toast, sizeof(app->toast), "车被收走了，押金不退");
            app->car_lost = false;
        } else if (app->st.sleep_debt >= 4) {
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
void fb_app_clear_dirty(fb_app_t *app) { app->dirty = false; }
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
            /* 没装备的行当也放进去。动作页本来就写好了「先置办…」+ 价格 +
             * 「钱不够去借钱」的界面，可这里原先一道 toast 把人拦在门外，
             * 那个入口玩家永远看不到 —— 屏幕上的「选不了」就是这一行造成的。
             * 现在按 OK 进去看代价，再按一次 OK 才决定掏钱还是去借。 */
            app->line_sel = app->sel;
            app->scr = FB_SCR_ACTION;
            app->sel = 0;
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
                        if (fb_buy_ebike(&app->st)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     "电动车到手，再按 OK 出工");
                        } else {
                            goto_finance_for(app, FB_LINE_DELIVERY);
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
                        if (fb_rent_car(&app->st)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     "车租好了，再按 OK 上路");
                        } else {
                            goto_finance_for(app, FB_LINE_RIDE);
                        }
                    } else if (can_run(app, FB_LINE_RIDE)) {
                        fb_run_ride(&app->st, app->toast, sizeof(app->toast));
                        back_to_home_after_work(app);
                    }
                    break;

                case FB_LINE_LIVE:
                    if (!fb_line_unlocked(&app->st, FB_LINE_LIVE)) {
                        if (fb_buy_live_gear(&app->st)) {
                            snprintf(app->toast, sizeof(app->toast),
                                     "设备到手，再按 OK 开播");
                        } else {
                            goto_finance_for(app, FB_LINE_LIVE);
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
                fb_apply_choice(&app->st, app->ev, app->sel, msg,
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
