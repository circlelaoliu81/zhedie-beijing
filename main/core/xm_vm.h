#pragma once
/* 双端视图模型：固件 LVGL 与主机 PNG 预览共用同一份纯 C11 页面描述。 */

#include "xm_huangli.h"
#include "xm_types.h"

#define XM_VIEW_LINE_MAX 24u
/* 设备只有 12/16/20/24 四档中文字体子集（assets/fonts/xm_font_*.c，
 * 四档共用同一份 charset.txt）。用户真机反馈「内容页字号太小」，
 * 内容区正文一律用 16px；顶栏状态条、异常条、来源/页脚这类辅助信息
 * 才允许 12px。新增上屏行必须用这两个常量，不许再写裸 12u 正文。 */
#define XM_FONT_BODY 16u
#define XM_FONT_AUX  12u
/* 两栏各 112px：最坏「凶煞：阳破阴冲」= 48px 前缀 + 64px 四字词 = 112px，
 * 108px 会超宽 4px（1096 天全量扫描实测）。左栏 x=4、右栏 x=124，
 * 中间留 8px 间隙并在 x=120 画一条淡赭竖线，两侧各留 4px 边距。 */
#define XM_PAGES_COL_W 112u
/* 摇卦动画一帧要画三枚方孔铜钱（外框+孔）+ 六行状态标记，16 个不够用。
 * 容量放大不影响栈：art 是指针，view 本体只增加 16 个 box 的常驻空间。 */
#define XM_VIEW_BOX_MAX 40u
#define XM_VIEW_STROKE_MAX 16u
/* 图标 sprite：状态条 2 + 数值卡 4 + 菜单卡 6 = 12 是单页上限。
 * 每个 12 字节，12 个给 xm_view_t 增加 144 字节栈占用（真机已知栈紧，所以卡死上限）。 */
#define XM_VIEW_ICON_MAX 12u

/* 折叠北京 · 城市夜配色（与基线的宣纸古风彻底区分）
 * 深底浅字：夜间屏幕更省电，也让 240x320 小屏的文字对比度更高。 */
#define XM_COLOR_PAPER   0x12151Cu   /* 背景：夜色底 */
#define XM_COLOR_INK     0xEDE6D8u   /* 主文字：暖白 */
#define XM_COLOR_SUBINK  0x8A8F99u   /* 次要文字：灰 */
#define XM_COLOR_RED     0xC4443Cu   /* 危险：红 */
#define XM_COLOR_BLUE    0x4FA88Bu   /* 成功：青 */
#define XM_COLOR_LINE    0x2A3040u   /* 分隔线：深灰蓝 */
#define XM_COLOR_DISABLE 0x5A6070u   /* 禁用：暗灰 */

typedef enum {
    XM_ALIGN_LEFT = 0,
    XM_ALIGN_CENTER,
    XM_ALIGN_RIGHT
} xm_view_align_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint8_t size;
    uint32_t color;
    uint8_t align;
    const char *text;
} xm_view_line_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    uint32_t color;
    bool fill;
    bool border;
    uint8_t radius;   /* 圆角半径：摇卦铜钱用「外圆内方」，0 = 直角 */
} xm_view_box_t;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    uint32_t color;
    uint8_t width;
} xm_view_stroke_t;

/* 图标 sprite。data 指向 Flash 里的 RGB565A8 常量（RGB565 平面 + A8 平面），
 * 因此图标可以压在任意底色上 —— 这是社区做法，纯 RGB565 会露出方块底。
 * 与 image 一样只存指针，不搬像素。 */
typedef struct {
    int16_t x;
    int16_t y;
    const uint8_t *data;
    uint8_t w;
    uint8_t h;
    uint8_t id;   /* 应用层图标 id；host 预览据此选同名 PNG，设备端不用 */
} xm_view_icon_t;

typedef struct {
    xm_view_line_t lines[24];
    xm_view_box_t boxes[32];
    xm_view_stroke_t strokes[16];
    /* 天师白描底图：RGB565 常量数组，LVGL lv_image 直读 Flash。
     * 只存指针不存像素，避免 xm_view_t 按值返回吃掉真机 8KB 任务栈
     * （历史栈溢出根因）。像素源与 host 预览同源，由门禁逐像素校验。 */
    const uint16_t *image;
    int16_t image_x;
    int16_t image_y;
    uint16_t image_w;
    uint16_t image_h;
    uint8_t image_id;   /* 应用层场景 id；host 预览据此选同名 PNG */
    xm_view_icon_t icons[XM_VIEW_ICON_MAX];
    uint8_t line_n;
    uint8_t box_n;
    uint8_t stroke_n;
    uint8_t icon_n;
    uint8_t overlay;
    bool footer;
} xm_view_t;

typedef struct {
    xm_page_t page;
    xm_datetime_t datetime;
    char date_text[32];
    char time_text[16];
    char cast_text[32];
    char cast_prompt[64];     /* 摇卦页动态按键提示：明确「按确定键摇第 N 爻」 */
    uint8_t cast_phase;       /* 0=等待确定键，1=本爻摇动中 */
    uint8_t cast_frame;       /* 0..7，输入任务 100ms tick 推进 */
    char notice_text[32];
    char today_line[64];
    char hl_lines[14][128];
    uint8_t hl_line_n;
    /* 黄历页两栏版式的分界（由 xm_pages.c 填，VM 只消费）：
     * [0, hl_head_n) 是全宽表头行；[hl_head_n, hl_col_split) 走左栏，
     * [hl_col_split, hl_line_n) 走右栏。字号从 12 提到 16 后一屏放不下 14 行，
     * 两栏把 14 个文本行压成 7~10 个视觉行，信息一条不丢、键位一个不改。 */
    uint8_t hl_head_n;
    uint8_t hl_col_split;
    bool hl_valid;
    xm_hl_day_t hl;
    xm_liuyao_t ly;
    uint8_t cast_toss[6];
    char result_lines[8][160];
    uint8_t result_line_n;
    xm_hist_rec_t history[8];
    char history_text[8][80];
    uint8_t history_count;
    bool history_empty;
    char about_lines[10][160];
    uint8_t about_line_n;
    char version[32];
    char build_id[96];
    uint8_t err_flags;
    uint8_t sel;
    uint8_t sub;
    uint8_t cast_count;
    uint8_t hour_index;
    /* 授时页当前编辑字段（0=年 1=月 2=日 3=时 4=分），由 xm_nav_t.time_cursor 透传。
     * 早先状态机里改 cursor、VM 却从不读，用户双击确定键切字段屏上毫无变化。 */
    uint8_t time_cursor;
    /* 授时页 5 个字段的文本（"2026"/"11"/"13"/"10"/"30"），供分段绘制用。
     * 必须各自成块：xm_view_t 只存 const char*，直接指向 date_text 中间会导致截断。 */
    char time_field_text[5][8];
    char time_progress[32];   /* 授时页「第 N / 5 段」进度，键位与推进都看得见 */
    bool notice;
    bool review_mode;
    bool settings_confirm_clear;
    int battery_soc;
} xm_page_state_t;

void xm_vm_state_init(xm_page_state_t *state);
xm_view_t xm_vm_render(xm_page_t page, const xm_page_state_t *state);
const char *xm_vm_overlay_text(uint8_t flags);
