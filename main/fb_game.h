#pragma once
/* 折叠北京 · 生存引擎（纯 C11，不依赖 ESP-IDF / LVGL，可 host 测试）
 *
 * 移植自 H5《活着》v4.6 的五线求生玩法：
 *   摆摊 / 外卖 / 日结 / 网约车 / 直播，100 天，健康见底或净资产崩盘即结束。
 * 所有随机都走 fb_state_t.seed 的 xorshift32，保证同一存档可复现，
 * 也便于 host 测试用固定 seed 断言数值。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FB_TOTAL_DAYS        100
#define FB_KIND_COUNT        5     /* 摆摊货品种类 */
#define FB_GIG_COUNT         6
#define FB_TOPIC_COUNT       4
#define FB_LINE_COUNT        5
#define FB_EVENT_POOL        18
#define FB_LOG_SIZE          160

/* 职业线枚举：与 UI 上「上/下」翻页顺序一致 */
typedef enum {
    FB_LINE_MARKET = 0,   /* 拼夕夕摆摊，零门槛 */
    FB_LINE_DELIVERY,     /* 送外卖，需电动车+装备 */
    FB_LINE_GIG,          /* 日结零工，零门槛 */
    FB_LINE_RIDE,         /* 网约车，需租车押金 */
    FB_LINE_LIVE,         /* 直播自媒体，需设备 */
    FB_LINE_NONE = 0xFF
} fb_line_t;

typedef enum {
    FB_END_NONE = 0,
    FB_END_RICH,          /* 净资产 >= 45000 */
    FB_END_SETTLE,        /* >= 12000 */
    FB_END_ALIVE,         /* >= 0 */
    FB_END_HOME,          /* >= -5000 */
    FB_END_HOMELESS,      /* < -5000 */
    FB_END_DEAD           /* 健康归零 */
} fb_ending_t;

/* 开局背景：手头宽紧与可刷卡额度不同 */
typedef struct {
    const char *name;
    const char *note;
    int32_t cash;
    int32_t credit;
    uint8_t debt;
} fb_bg_t;

typedef struct {
    const char *name;
    int32_t cost;     /* 进货价 */
    int32_t price;    /* 零售价 */
    uint8_t base;     /* 基准日需求 */
    uint8_t stamina;  /* 出摊耗体力 */
    bool spoil;       /* 易坏，下雨需求打折 */
} fb_item_t;

typedef struct {
    const char *name;
    const char *note;
    int32_t wage;
    uint8_t stamina;
} fb_gig_t;

typedef struct {
    const char *name;
    const char *note;
    int32_t cost;     /* 自付成本，如探店费 */
    uint8_t fans;     /* 基础涨粉 */
    uint8_t gift_pct; /* 礼物系数 ×100 */
    uint8_t risk_pct; /* 翻车概率 % */
} fb_topic_t;

typedef struct {
    const char *name;
    const char *desc;
    int32_t startup;
    uint8_t requires;  /* 0=无 1=电动车 2=车 3=直播设备 */
} fb_line_meta_t;

/* 事件选项：字段增量全部为整数，special 交给引擎处理特殊逻辑 */
typedef struct {
    const char *label;
    int16_t cash;
    int16_t health;
    int16_t stamina;
    int16_t rep;
    int16_t fans;
    uint8_t special;   /* 0=无 1=第二条命(父母) 2=赌一把 3=掉粉4成 */
} fb_choice_t;

typedef struct {
    const char *title;
    const char *text;
    uint8_t weight;
    bool once;
    uint8_t line_mask;       /* 0=通用；否则 1<<fb_line */
    uint8_t choice_n;
    fb_choice_t choices[3];
} fb_event_t;

typedef struct {
    uint32_t seed;
    int16_t day;             /* 1..FB_TOTAL_DAYS */
    int32_t cash;
    int32_t debt;
    int16_t health;          /* 0..100 */
    int16_t stamina;         /* 0..100 */
    int16_t reputation;      /* 0..100 */
    uint16_t storage;        /* 摆摊仓储格数 */
    uint16_t fans;           /* 直播粉丝 */
    int16_t rent_paid_until; /* 租车月租有效期（第几天前有效） */
    uint8_t has_ebike;
    uint8_t has_gear;
    uint8_t has_car;
    uint8_t has_live;
    uint8_t hours_used;      /* 今日已消耗工时（上限 16） */
    uint8_t inv[FB_KIND_COUNT];
    uint8_t inv_total;
    uint8_t family_used;     /* 第二条命一局一次 */
    uint8_t once_mask;       /* once 事件已用位图（按事件序号） */
    uint8_t recent[3];       /* 最近事件序号，做去重窗口 */
    uint8_t recent_n;
    uint8_t bg;              /* 开局背景序号 */
    uint8_t ending;          /* fb_ending_t */
    uint8_t today_lines;     /* 今日已干过的线位图 */
    int32_t day_earned;
    int32_t day_spent;
    /* 摆摊当日需求：regen 时按点×货品生成 */
    uint8_t demand[FB_KIND_COUNT];
    uint8_t spot;            /* 摆摊点序号 */
    int16_t quote_idx;       /* 每日语录轮换 */
} fb_state_t;

/* —— 数据访问 —— */
const fb_bg_t *fb_bg_table(size_t *n);
const fb_item_t *fb_item_table(size_t *n);
const fb_gig_t *fb_gig_table(size_t *n);
const fb_topic_t *fb_topic_table(size_t *n);
const fb_line_meta_t *fb_line_table(size_t *n);
const fb_event_t *fb_event_table(size_t *n);
const char *fb_end_title(uint8_t ending);
const char *fb_end_text(uint8_t ending);
const char *fb_quote(int32_t idx);
const char *fb_news(int32_t idx, uint32_t seed);
size_t fb_quote_count(void);
size_t fb_news_count(void);
const char *fb_spot_name(uint8_t spot);
size_t fb_spot_count(void);

/* —— 生命周期 —— */
void fb_init(fb_state_t *st, uint8_t bg, uint32_t seed);
void fb_regen_day(fb_state_t *st);
bool fb_check_end(fb_state_t *st);   /* 返回 true 表示本局已结束 */

/* —— 随机（确定性） —— */
uint32_t fb_rand(fb_state_t *st);
int32_t fb_range(fb_state_t *st, int32_t lo, int32_t hi);
bool fb_chance(fb_state_t *st, uint32_t pct);

/* —— 派生数值 —— */
int32_t fb_net_worth(const fb_state_t *st);
int32_t fb_inv_value(const fb_state_t *st);
uint8_t fb_hours_left(const fb_state_t *st);
bool fb_line_unlocked(const fb_state_t *st, uint8_t line);
const char *fb_unlock_hint(unsigned line);

/* —— 融资 —— */
bool fb_cashout(fb_state_t *st, int32_t amount);
bool fb_loan(fb_state_t *st, int32_t amount);
bool fb_repay(fb_state_t *st, int32_t amount);

/* —— 摆摊 —— */
bool fb_buy_item(fb_state_t *st, uint8_t kind, uint8_t qty);
bool fb_expand_storage(fb_state_t *st);
int32_t fb_run_market(fb_state_t *st, char *log, size_t logsz);

/* —— 其余工作线 —— */
bool fb_buy_ebike(fb_state_t *st);
bool fb_rent_car(fb_state_t *st);
bool fb_buy_live_gear(fb_state_t *st);
int32_t fb_run_delivery(fb_state_t *st, char *log, size_t logsz);
int32_t fb_run_gig(fb_state_t *st, uint8_t gig, char *log, size_t logsz);
int32_t fb_run_ride(fb_state_t *st, char *log, size_t logsz);
int32_t fb_run_live(fb_state_t *st, uint8_t topic, char *log, size_t logsz);

/* —— 休息：当天不干活，额外回复健康与体力，但仍结算当日开销 —— */
void fb_rest(fb_state_t *st);

/* —— 日结算 —— */
void fb_end_day(fb_state_t *st, char *log, size_t logsz);
/* 结算并推进到下一天（重置今日工时/流水，重算摆摊需求）。
 * 调用方应在事件处理完后调用，然后立刻 fb_check_end()。 */
void fb_advance_day(fb_state_t *st);

/* —— 事件 —— */
const fb_event_t *fb_roll_event(fb_state_t *st);
void fb_apply_choice(fb_state_t *st, const fb_event_t *ev, uint8_t choice,
                     char *log, size_t logsz);
void fb_skip_event(fb_state_t *st);
