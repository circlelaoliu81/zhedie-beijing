#pragma once
/* 折叠北京 · 应用编排层（L4）
 *
 * 职责：三键输入 → 页面状态机 → 组装 fb_view_t。不直接触碰 LVGL，
 * 渲染由 xm_ui_apply_locked() 完成，因此本层可在 host 上测试。
 *
 * 文本池：xm_ui 用 lv_label_set_text_static() 保存指针而非拷贝，
 * 所以每行文本必须常驻。所有格式化字符串都写进 app 自带的 s_text_pool。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fb_game.h"
#include "xm_vm.h"

#define FB_TEXT_ROWS 40
#define FB_TEXT_COLS 52
#define FB_STACK_MAX 4

typedef enum {
    FB_SCR_INTRO = 0,   /* 封面与开局背景选择 */
    FB_SCR_HOME,        /* 主面板：状态 + 菜单 */
    FB_SCR_LINES,       /* 五条职业线 */
    FB_SCR_ACTION,      /* 当前线下的具体动作 */
    FB_SCR_FINANCE,     /* 借钱/还债 */
    FB_SCR_STORAGE,     /* 仓储扩容 */
    FB_SCR_REPORT,      /* 今日结算报告 */
    FB_SCR_EVENT,       /* 随机事件 */
    FB_SCR_ENDING,      /* 结局 */
    FB_SCR_ABOUT,       /* 规则说明 */
    FB_SCR_COUNT
} fb_scr_t;

/* 主菜单项 */
typedef enum {
    FB_MENU_WORK = 0,
    FB_MENU_REST,
    FB_MENU_FINANCE,
    FB_MENU_STORAGE,
    FB_MENU_ENDDAY,
    FB_MENU_ABOUT,
    FB_MENU_COUNT
} fb_menu_t;

typedef struct {
    fb_state_t st;
    fb_scr_t scr;
    uint8_t sel;              /* 当前页选中项 */
    uint8_t bg_sel;           /* 开局背景选择 */
    uint8_t line_sel;         /* 职业线选择 */
    uint8_t home_sel;         /* 主面板记忆光标：从子页返回时回到原来那一项 */
    uint8_t stack[FB_STACK_MAX];
    uint8_t depth;
    const fb_event_t *ev;     /* 当前事件（指针指向只读表） */
    uint8_t ev_choice;
    char toast[FB_LOG_SIZE];  /* 最近一次操作结果 */
    bool dirty;
    bool ended_shown;
    uint32_t seed;
    /* 常驻文本池：给 xm_view_line_t.text 提供稳定地址 */
    char text_pool[FB_TEXT_ROWS][FB_TEXT_COLS];
    uint8_t pool_used;
} fb_app_t;

void fb_app_init(fb_app_t *app, uint32_t seed);
void fb_app_on_key(fb_app_t *app, xm_input_t input);
bool fb_app_need_render(const fb_app_t *app);
void fb_app_mark_dirty(fb_app_t *app);

/* 把当前状态渲染成视图模型；返回的 text 指针指向 app->text_pool，调用方不得释放 */
void fb_app_render(fb_app_t *app, xm_view_t *out);
