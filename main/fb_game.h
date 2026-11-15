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
#define FB_SPOT_COUNT        8     /* 摆摊点位（夜市/商圈/城中村…，需求倍率各不同） */
#define FB_GIG_COUNT         6
#define FB_TOPIC_COUNT       4
#define FB_LINE_COUNT        5
#define FB_EVENT_POOL        20   /* 18 条常规 + 2 条瞌睡专属（见 EV_IDX_DROWSY_*）*/
#define FB_LOG_SIZE          160

/* 一天 24 小时 = 干活 + 睡觉。
 * 这不是「工时上限」，而是这一天的全部时间：干 18 小时就只剩 6 小时睡。
 * 睡不够按档扣健康并累积睡眠债，债多了白天犯困（收入打折、意外变多），
 * 再顶着干就会晕倒。想多挣只能从睡觉里抠 —— 这是本作唯一的真资源。 */
#define FB_DAY_HOURS         24
/* 睡眠分档：>=8 睡够，6~8 欠一点，4~6 明显缺，2~4 严重缺，<2 通宵 */
#define FB_SLEEP_OK          8
#define FB_SLEEP_SHORT       6
#define FB_SLEEP_CRIT        4
#define FB_SLEEP_NONE        2
/* 睡眠债最多记 7 天，封顶防止无限叠加 */
#define FB_SLEEP_DEBT_MAX    7
/* 工时超过这个数开始算加班透支（日结算按超标比例抽一次健康事件） */
#define FB_OVERTIME_HOURS    14
/* 直播连播：连播天数每 +1 涨粉 +10%，连满 10 天封顶 ×2.0 */
#define FB_LIVE_STREAK_MAX   10

/* 瞌睡等级：0 精神 / 1 犯困 / 2 撑不住 */
typedef enum {
    FB_DROWSY_NONE = 0,
    FB_DROWSY_TIRED,
    FB_DROWSY_CRITICAL
} fb_drowsy_t;

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
    uint8_t vol_pct;  /* 日需求波动幅度 %（点位需求 = base × bias × (1 ± vol)） */
    bool spoil;       /* 易坏，下雨需求打折 */
} fb_item_t;

/* 摆摊点位：同一个摊子摆在夜市和摆在公园，日需求差出五成。
 * bias_pct 是需求倍率的百分数（130 = ×1.30），玩家据此挑点位。 */
typedef struct {
    const char *name;
    uint8_t bias_pct;
} fb_spot_t;

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
    int32_t savings;         /* 存款：日息 1%，不到 1 元按 1 元算 */
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
    uint8_t hours_used;      /* 今日已干的小时数（0..FB_DAY_HOURS），剩余时间都在睡 */
    uint8_t inv[FB_KIND_COUNT];
    uint8_t inv_total;
    uint8_t family_used;     /* 第二条命一局一次 */
    uint8_t once_mask;       /* once 事件已用位图（按事件序号） */
    uint8_t recent[3];       /* 最近事件序号，做去重窗口 */
    uint8_t recent_n;
    uint8_t bg;              /* 开局背景序号 */
    uint8_t ending;          /* fb_ending_t */
    uint8_t today_lines;     /* 今日已干过的线位图 */
    /* —— 睡眠剥夺链 —— */
    uint8_t last_sleep;      /* 昨夜实际睡了几小时（= FB_DAY_HOURS - 昨日工时） */
    uint8_t sleep_debt;      /* 连续缺觉天数 0..FB_SLEEP_DEBT_MAX */
    uint8_t drowsy;          /* fb_drowsy_t：白天瞌睡等级 */
    uint8_t faint_cnt;       /* 本局晕倒次数 */
    uint8_t recovering;      /* 晕倒后第二天只能干半天，1 = 生效中 */
    int32_t day_earned;
    int32_t day_spent;
    /* 摆摊当日需求：[点位][货品]。整天不重摇 —— 切点位只换视角，
     * 否则玩家可以反复切换刷出高需求点位，这等于免费重开骰子。 */
    uint8_t demand[FB_SPOT_COUNT][FB_KIND_COUNT];
    uint8_t spot;            /* 当前摆摊点序号 */
    /* 直播连播：连续开播天数，断一天清零。涨粉加成 = 1 + 10% × min(streak, 10)。 */
    uint8_t live_streak;
    uint8_t news_idx;        /* 今日新闻序号（k_news 下标） */
} fb_state_t;

/* —— 数据访问 —— */
const fb_bg_t *fb_bg_table(size_t *n);
const fb_item_t *fb_item_table(size_t *n);
const fb_gig_t *fb_gig_table(size_t *n);
const fb_topic_t *fb_topic_table(size_t *n);
const fb_line_meta_t *fb_line_table(size_t *n);
const fb_event_t *fb_event_table(size_t *n);
const fb_spot_t *fb_spot_table(size_t *n);
const char *fb_end_title(uint8_t ending);
const char *fb_end_text(uint8_t ending);
/* 今日新闻：按 st->news_idx 取句，每天换一条 */
const char *fb_news_today(const fb_state_t *st);
const char *fb_news(int32_t idx, uint32_t seed);
size_t fb_news_count(void);
const char *fb_spot_name(uint8_t spot);
size_t fb_spot_count(void);

/* —— 每日语录 ——
 * 两个池子各 18 条：丧丧语录（低气压、北漂真实体感）与心灵毒鸡汤（把安慰拧成扎心）。
 * 分两个槽位，保证同一天不会看到同一句：
 *   slot 0 = 当天开场横幅（按天数奇偶交替两个池子）
 *   slot 1 = 日终旁白（按当日盈亏选语气：亏了陪着丧，挣了扎一刀）
 * fb_quote_daily_kind() 返回 0=丧 / 1=毒鸡汤，UI 据此换颜色。 */
typedef enum {
    FB_QUOTE_SANG = 0,
    FB_QUOTE_POISON = 1
} fb_quote_kind_t;

const char *fb_quote_daily(int16_t day, uint8_t slot, int32_t net_today);
uint8_t fb_quote_daily_kind(int16_t day, uint8_t slot, int32_t net_today);
size_t fb_quote_count(uint8_t kind);

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
/* 今天还剩几小时能干（= FB_DAY_HOURS - hours_used，剩下的时间就是睡觉时间） */
uint8_t fb_hours_left(const fb_state_t *st);
/* 按当前工时推算「今晚能睡几小时」。干满 24 小时 = 0，通宵。 */
uint8_t fb_sleep_hours(const fb_state_t *st);
/* 该线干一次要几小时：摆摊 5 / 外卖 6 / 日结 8 / 网约车 8 / 直播 3。
 * UI 层据此判断「今天还够不够再打一份工」，不许另抄一份数字。 */
uint8_t fb_line_hours(uint8_t line);
bool fb_line_unlocked(const fb_state_t *st, uint8_t line);
const char *fb_unlock_hint(unsigned line);
/* 瞌睡等级的中文标签（精神/犯困/撑不住），UI 直接贴 */
const char *fb_drowsy_label(uint8_t drowsy);

/* —— 融资 —— */
bool fb_cashout(fb_state_t *st, int32_t amount);
bool fb_loan(fb_state_t *st, int32_t amount);
bool fb_repay(fb_state_t *st, int32_t amount);

/* —— 银行：存活期拿日息 1%，但钱不在手上就掏不出医药费 —— */
bool fb_bank_deposit(fb_state_t *st, int32_t amount);
bool fb_bank_withdraw(fb_state_t *st, int32_t amount);

/* —— 摆摊 —— */
bool fb_buy_item(fb_state_t *st, uint8_t kind, uint8_t qty);
bool fb_expand_storage(fb_state_t *st);
/* 换摆摊点：只换视角，不动当日需求矩阵（防止反复切换刷高需求） */
bool fb_set_spot(fb_state_t *st, uint8_t spot);
/* 某点位某货品的当日需求，UI 用它在选点位上显示「夜市 ×1.3」和预计能卖多少 */
uint8_t fb_demand_at(const fb_state_t *st, uint8_t spot, uint8_t kind);
int32_t fb_run_market(fb_state_t *st, char *log, size_t logsz);

/* —— 其余工作线 —— */
bool fb_buy_ebike(fb_state_t *st);
bool fb_rent_car(fb_state_t *st);
bool fb_buy_live_gear(fb_state_t *st);
int32_t fb_run_delivery(fb_state_t *st, char *log, size_t logsz);
int32_t fb_run_gig(fb_state_t *st, uint8_t gig, char *log, size_t logsz);
int32_t fb_run_ride(fb_state_t *st, char *log, size_t logsz);
int32_t fb_run_live(fb_state_t *st, uint8_t topic, char *log, size_t logsz);
/* 今天的涨粉加成百分数（100 = 无加成，200 = 满级翻倍）。
 * 连播第 N 天 = 100 + 10×(N-1)，封顶 200。UI 直接贴数字。 */
uint8_t fb_live_bonus_pct(const fb_state_t *st);

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
