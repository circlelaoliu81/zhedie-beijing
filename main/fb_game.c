/* 折叠北京 · 生存引擎实现
 *
 * 数值口径沿用 H5 版《活着》v4.6（其注释标明来自 2024-2025 公开行情：
 * 外卖每单 6~9 元、合租 1500~3000/月、电动车 2500+装备 300、网约车押金 8000、
 * 直播设备 1800、盒饭 12~20）。为适配 240×320 三键小屏，事件池从 40+ 精简到
 * 18 条，保留通用线与五条职业线各 2~3 条代表性痛点；文案同步压短。
 */

#include "fb_game.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ============================ 数值常量 ============================ */
#define FB_START_CASH        1600
#define FB_START_HEALTH      90
#define FB_START_STAMINA     100
#define FB_START_REP         55
#define FB_HOURS_CAP         16
#define FB_MEAL              18
#define FB_METRO             4
#define FB_CHARGE            8
#define FB_RENT_MONTHLY      2000
#define FB_PHONE_MONTHLY     29
#define FB_UTILITY_MONTHLY   30
#define FB_EBIKE_COST        2500
#define FB_EBIKE_GEAR        300
#define FB_RIDE_DEPOSIT      8000
#define FB_RIDE_RENT         2800
#define FB_RIDE_RENT_DAYS    30
#define FB_LIVE_GEAR         1800
#define FB_LIVE_FANS_CAP     5000
#define FB_CASHOUT_FEE_PCT   3
#define FB_CASHOUT_RATE_PCT  15    /* 0.15% 日息 → 按万分比整数运算 */
#define FB_LOAN_RATE_PCT     100   /* 1% 日息 */
#define FB_DEBT_MIN_INTEREST 2
#define FB_CASH_BANKRUPT     (-1500)
#define FB_FAMILY_CASH       2200
#define FB_FAMILY_HEALTH     25
#define FB_FAMILY_REP        10
#define FB_STORAGE_START     16
#define FB_STORAGE_STEP      8
#define FB_STORAGE_COST      600

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void logf_(char *log, size_t logsz, const char *fmt, ...) {
    if (!log || logsz == 0) return;
    size_t used = strlen(log);
    if (used + 1 >= logsz) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log + used, logsz - used, fmt, ap);
    va_end(ap);
}

/* ============================ 数据表 ============================ */

static const fb_bg_t k_bg[] = {
    { "刚被裁员", "卡里就这些，额度有限", 1600, 3000, 0 },
    { "攒了点钱", "前几年存下的，够小本买卖", 2600, 3000, 0 },
    { "卡还能刷", "手头紧，但有张大额卡", 1600, 6000, 0 },
};

static const fb_item_t k_items[FB_KIND_COUNT] = {
    { "烤冷面", 4, 8, 30, 26, true },
    { "臭豆腐", 3, 6, 24, 24, true },
    { "烤肠", 2, 5, 30, 24, true },
    { "冰粉", 2, 7, 22, 22, true },
    { "贴膜", 2, 15, 8, 18, false },
};

static const fb_gig_t k_gigs[FB_GIG_COUNT] = {
    { "库房搬箱", "计件，扛到收工", 200, 28 },
    { "工地小工", "日结，晒一天", 260, 34 },
    { "发传单", "站街，最轻", 120, 14 },
    { "保洁小时", "写字楼，体面点", 160, 20 },
    { "快递分拣", "夜班居多", 180, 24 },
    { "商超配送", "短途，不淋雨", 150, 22 },
};

static const fb_topic_t k_topics[FB_TOPIC_COUNT] = {
    { "北漂日常", "稳，涨粉慢", 0, 6, 100, 2 },
    { "探店吃播", "自掏探店费", 60, 10, 140, 6 },
    { "才艺搞笑", "波动大，可能爆火", 0, 14, 180, 12 },
    { "蹭热点", "涨粉快，易翻车", 0, 20, 220, 20 },
};

static const fb_line_meta_t k_lines[FB_LINE_COUNT] = {
    { "拼夕夕摆摊", "低价进货，摆摊零售赚差价", 0, 0 },
    { "送外卖", "先买电动车，接单跑量", 2800, 1 },
    { "日结零工", "桥头蹲活儿，当天领钱", 0, 0 },
    { "网约车", "租车跑活，押金高，常要借钱", 8000, 2 },
    { "直播自媒体", "买设备开播，靠打赏和商单", 1800, 3 },
};

static const char *k_spots[] = { "夜市", "商圈", "城中村", "写字楼",
                                 "学校", "地铁口", "医院", "公园" };

/* 18 条事件：line_mask=0 通用，否则只在该线当天干活后可能触发 */
static const fb_event_t k_events[FB_EVENT_POOL] = {
    { "夜里发起烧", "隔断间漏风，你烧到三十九度。硬扛还是去医院？", 10, false, 0, 2,
      { { "硬扛，-8 健康", 0, -8, 0, 0, 0, 0 },
        { "看门诊，-500", -500, 6, 0, 0, 0, 0 } } },
    { "老乡来借钱", "老乡张口借两千周转，说下月还。", 7, false, 0, 2,
      { { "推了，名声 -1", 0, 0, 0, -1, 0, 0 },
        { "借，-2000", -2000, 0, 0, 2, 0, 0 } } },
    { "捡到个钱包", "共享单车筐里有个钱包，三百现金。", 6, true, 0, 2,
      { { "拿走，+300", 300, 0, 0, -2, 0, 0 },
        { "交给岗亭，名声 +3", 0, 0, 0, 3, 0, 0 } } },
    { "路口查车", "交警设卡，电动车没牌被拦下。", 5, false, 1u << FB_LINE_DELIVERY, 2,
      { { "认罚，-200", -200, 0, 0, -1, 0, 0 },
        { "求情，看名声", 0, 0, 0, 1, 0, 0 } } },
    { "妈打来电话", "妈听出你声音哑了，问要不要回家。", 4, false, 0, 2,
      { { "接这通电话（第二条命）", 0, 0, 0, 0, 0, 1 },
        { "先不接", 0, 0, 0, 0, 0, 0 } } },
    { "刮刮乐中五十", "便利店刮刮乐居然中了五十。", 3, true, 0, 1,
      { { "收下，+50", 50, 0, 0, 0, 0, 0 } } },
    { "楼下大爷塞药", "大爷说你脸色差，塞给你一盒感冒药。", 4, true, 0, 1,
      { { "收下，+4 健康", 0, 4, 0, 0, 0, 0 } } },
    { "城管来了", "远远看见执法车，你的摊子还支着。", 6, false, 1u << FB_LINE_MARKET, 3,
      { { "认罚，-250", -250, 0, 0, -1, 0, 0 },
        { "收摊跑路，-20 精力", 0, 0, -20, 0, 0, 0 },
        { "托熟人，-150", -150, 0, 0, 0, 0, 0 } } },
    { "视频被举报", "你蹭的热点踩了线，号被限流。", 5, false, 1u << FB_LINE_LIVE, 2,
      { { "删了息事，掉粉四成", 0, 0, 0, -2, 0, 3 },
        { "硬刚到底，赌一把", 0, 0, 0, 1, 0, 2 } } },
    { "催债电话", "网贷催收一天打了八个，利息越滚越大。", 4, false, 0, 2,
      { { "先还一点，-300", -300, 0, 0, 0, 0, 0 },
        { "装没听见，名声 -3", 0, 0, 0, -3, 0, 0 } } },
    { "手机摔了", "跑单接活全靠它，屏幕碎成蜘蛛网。", 4, false, 0, 2,
      { { "换屏，-350", -350, 0, 0, 0, 0, 0 },
        { "将就用，名声 -2", 0, 0, 0, -2, 0, 0 } } },
    { "房东要涨租", "隔断间要拆，要么加钱，要么搬更远。", 4, false, 0, 2,
      { { "加钱续租，-300", -300, 0, 0, 0, 0, 0 },
        { "搬去更远，名声 -1", 0, 0, 0, -1, 0, 0 } } },
    { "病倒了", "连着熬夜接单，这回真扛不住了。", 5, false, 0, 2,
      { { "硬扛，-12 健康", 0, -12, 0, 0, 0, 0 },
        { "挂急诊，-600", -600, 8, 0, 0, 0, 0 } } },
    { "平台冻结提现", "风控把你标了，今天的收入提不出来。", 4, false,
      (1u << FB_LINE_DELIVERY) | (1u << FB_LINE_RIDE) | (1u << FB_LINE_LIVE), 2,
      { { "写申诉，名声 +2", 0, -4, 0, 2, 0, 0 },
        { "认栽，名声 -1", 0, 0, 0, -1, 0, 0 } } },
    { "运管查车", "路口临检，顺风车单被抓现行。", 4, false, 1u << FB_LINE_RIDE, 2,
      { { "认罚，-260", -260, 0, 0, 0, 0, 0 },
        { "装糊涂，看名声", 0, 0, 0, 1, 0, 0 } } },
    { "同行扎堆", "夜市新来三家摊，跟你卖一样的货。", 4, false, 1u << FB_LINE_MARKET, 2,
      { { "降价抢客，名声 -1", 0, 0, 0, -1, 0, 0 },
        { "守着，认了", 0, 0, 0, 0, 0, 0 } } },
    { "刷到朋友圈", "同学晒了新车新房，你缩在隔断间里。", 3, false, 0, 2,
      { { "关掉手机，+2 健康", 0, 2, 0, 0, 0, 0 },
        { "接着刷，-2 健康", 0, -2, 0, 0, 0, 0 } } },
    { "算法又砍五分钟", "配送时间又缩了五分钟，这单铁定超时。", 4, false,
      1u << FB_LINE_DELIVERY, 2,
      { { "认罚，-120", -120, 0, 0, -1, 0, 0 },
        { "闯红灯赶时间，-8 健康", 0, -8, 0, 0, 0, 0 } } },
};

static const char *k_end_title[] = {
    "", "登上北漂富人榜", "在北京站住了脚", "勉强活着",
    "收拾行李回了老家", "露宿桥洞", "病倒在冬天",
};

static const char *k_end_text[] = {
    "",
    "一百天，你从隔断间搬进自己租的两居。朋友说你运气好，只有你知道每个差价怎么来的。",
    "没大富大贵，但月末不再心慌。你续了下一季的房租。",
    "卡里总剩个三位数，房租靠东挪西凑。北京还是那个北京，你还在。",
    "某个加完班的深夜你算了笔账，买了张站票。火车启动时，天际线退成背景。",
    "窟窿越滚越大，房东换了锁。你最后把铺盖卷挪到了立交桥下。",
    "小病拖成大病，ICU 一天的费用抵你半月工资。故事停在了十一月。",
};

static const char *k_quotes[] = {
    "生活总在教我低头，可我连腰都弯不动了。",
    "北京很大，大到装不下我一个隔断间。",
    "钱没挣着，发际线先没了。",
    "不是不想家，是没脸回。",
    "今天的我，又是被生活按在地上摩擦的一天。",
    "梦想是很好，可惜房租不认。",
    "越熬越穷，越穷越熬。",
    "成年人的崩溃，都是从那句「我没事」开始的。",
    "别人那叫生活，我这叫活着。",
    "不敢生病，不敢辞职，不敢停下来。",
    "把日子过成了任务清单，勾完一项还有下一项。",
    "世上无难事，只要肯放弃。",
};

static const char *k_news[] = {
    "今晨地铁早高峰，西直门站限流。",
    "气象台：本周多雷阵雨，外卖订单暴涨。",
    "住建委通报：违规隔断房继续排查。",
    "共享单车又涨价，月卡涨到二十五。",
    "某平台调整派单，远单补贴上调。",
    "城中村改造推进，房租又传上调。",
    "招聘淡季，日结工价普遍下滑。",
    "新能源网约车补贴退坡，司机叫苦。",
    "某短视频平台整治低俗，一批号被封。",
    "消费贷广告铺天盖地，背后是日息陷阱。",
};

/* ============================ 数据访问 ============================ */

#define TABLE_N(a) (sizeof(a) / sizeof((a)[0]))
#define TBL_FN(fn, arr, type) \
    const type *fn(size_t *n) { if (n) *n = TABLE_N(arr); return arr; }

TBL_FN(fb_bg_table, k_bg, fb_bg_t)
TBL_FN(fb_item_table, k_items, fb_item_t)
TBL_FN(fb_gig_table, k_gigs, fb_gig_t)
TBL_FN(fb_topic_table, k_topics, fb_topic_t)
TBL_FN(fb_line_table, k_lines, fb_line_meta_t)
TBL_FN(fb_event_table, k_events, fb_event_t)

size_t fb_quote_count(void) { return TABLE_N(k_quotes); }
size_t fb_news_count(void) { return TABLE_N(k_news); }
size_t fb_spot_count(void) { return TABLE_N(k_spots); }

const char *fb_quote(int32_t idx) {
    if (idx < 0) return k_quotes[0];
    return k_quotes[(size_t)idx % TABLE_N(k_quotes)];
}

const char *fb_news(int32_t idx, uint32_t seed) {
    size_t i = ((size_t)(idx < 0 ? 0 : idx) + (seed % 7u)) % TABLE_N(k_news);
    return k_news[i];
}

const char *fb_spot_name(uint8_t spot) {
    return spot < TABLE_N(k_spots) ? k_spots[spot] : k_spots[0];
}

const char *fb_end_title(uint8_t ending) {
    return ending < TABLE_N(k_end_title) ? k_end_title[ending] : "";
}

const char *fb_end_text(uint8_t ending) {
    return ending < TABLE_N(k_end_text) ? k_end_text[ending] : "";
}

/* ============================ 随机 ============================ */

uint32_t fb_rand(fb_state_t *st) {
    uint32_t x = st->seed ? st->seed : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    st->seed = x;
    return x;
}

int32_t fb_range(fb_state_t *st, int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    return lo + (int32_t)(fb_rand(st) % (uint32_t)(hi - lo + 1));
}

bool fb_chance(fb_state_t *st, uint32_t pct) {
    if (pct == 0) return false;
    if (pct >= 100) return true;
    return (fb_rand(st) % 100u) < pct;
}

/* ============================ 生命周期 ============================ */

void fb_regen_day(fb_state_t *st) {
    const fb_item_t *it = k_items;
    for (uint8_t i = 0; i < FB_KIND_COUNT; ++i) {
        int32_t v = (int32_t)it[i].base + (int32_t)fb_range(st, -6, 8);
        st->demand[i] = (uint8_t)clamp32(v, 1, 60);
    }
}

void fb_init(fb_state_t *st, uint8_t bg, uint32_t seed) {
    memset(st, 0, sizeof(*st));
    const fb_bg_t *b = &k_bg[bg < TABLE_N(k_bg) ? bg : 0];
    st->bg = bg;
    st->seed = seed ? seed : 0x1234567u;
    st->day = 1;
    st->cash = b->cash;
    st->debt = b->debt;
    st->health = FB_START_HEALTH;
    st->stamina = FB_START_STAMINA;
    st->reputation = FB_START_REP;
    st->storage = FB_STORAGE_START;
    st->recent_n = 0;
    st->ending = FB_END_NONE;
    fb_regen_day(st);
}

int32_t fb_net_worth(const fb_state_t *st) {
    return st->cash + fb_inv_value(st) - st->debt;
}

int32_t fb_inv_value(const fb_state_t *st) {
    int32_t v = 0;
    for (uint8_t i = 0; i < FB_KIND_COUNT; ++i) v += (int32_t)st->inv[i] * k_items[i].cost;
    return v;
}

uint8_t fb_hours_left(const fb_state_t *st) {
    return st->hours_used >= FB_HOURS_CAP ? 0 : (uint8_t)(FB_HOURS_CAP - st->hours_used);
}

bool fb_line_unlocked(const fb_state_t *st, uint8_t line) {
    if (line >= FB_LINE_COUNT) return false;
    switch (k_lines[line].requires) {
        case 1: return st->has_ebike && st->has_gear;
        case 2: return st->has_car;
        case 3: return st->has_live;
        default: return true;
    }
}

const char *fb_unlock_hint(unsigned line) {
    if (line >= FB_LINE_COUNT) return "";
    switch (k_lines[line].requires) {
        case 1: return "需电动车+装备 ¥2800";
        case 2: return "需租车押金 ¥8000";
        case 3: return "需直播设备 ¥1800";
        default: return "";
    }
}

/* ============================ 融资 ============================ */

bool fb_cashout(fb_state_t *st, int32_t amount) {
    if (amount <= 0) return false;
    int32_t fee = (amount * FB_CASHOUT_FEE_PCT + 99) / 100;
    int32_t net = amount - fee;
    st->cash += net;
    st->debt += amount;
    return true;
}

bool fb_loan(fb_state_t *st, int32_t amount) {
    if (amount <= 0) return false;
    st->cash += amount;
    st->debt += amount;
    return true;
}

bool fb_repay(fb_state_t *st, int32_t amount) {
    int32_t r = amount;
    if (r > st->cash) r = st->cash;
    if (r > st->debt) r = st->debt;
    if (r <= 0) return false;
    st->cash -= r;
    st->debt -= r;
    return true;
}

/* ============================ 工时 ============================ */

/* 返回 false 表示今天工时不够；成功则扣工时并按时长掉健康 */
static bool spend_hours(fb_state_t *st, uint8_t line, uint8_t h) {
    if (h > fb_hours_left(st)) return false;
    st->hours_used += h;
    st->today_lines |= (uint8_t)(1u << line);
    /* 0.4 健康/工时 → 每 5 工时掉 2 点，用整数避免浮点 */
    int32_t loss = ((int32_t)h * 2) / 5;
    st->health = (int16_t)clamp32(st->health - loss, 0, 100);
    return true;
}

/* ============================ 摆摊 ============================ */

bool fb_buy_item(fb_state_t *st, uint8_t kind, uint8_t qty) {
    if (kind >= FB_KIND_COUNT || qty == 0) return false;
    if (st->inv_total + qty > st->storage) return false;
    int32_t cost = k_items[kind].cost * qty;
    if (st->cash < cost) return false;
    st->cash -= cost;
    st->day_spent += cost;
    st->inv[kind] = (uint8_t)(st->inv[kind] + qty);
    st->inv_total = (uint8_t)(st->inv_total + qty);
    return true;
}

bool fb_expand_storage(fb_state_t *st) {
    if (st->cash < FB_STORAGE_COST) return false;
    st->cash -= FB_STORAGE_COST;
    st->day_spent += FB_STORAGE_COST;
    st->storage = (uint16_t)(st->storage + FB_STORAGE_STEP);
    return true;
}

int32_t fb_run_market(fb_state_t *st, char *log, size_t logsz) {
    if (!log) return 0;
    log[0] = '\0';
    if (st->inv_total == 0) {
        logf_(log, logsz, "没进货，摊子支不起来");
        return 0;
    }
    if (!spend_hours(st, FB_LINE_MARKET, 5)) {
        logf_(log, logsz, "今天只剩 %u 小时，摆摊要 5 小时",
              (unsigned)fb_hours_left(st));
        return 0;
    }
    bool rain = fb_chance(st, 22);
    int32_t revenue = 0;
    uint8_t max_stamina = 0;
    for (uint8_t i = 0; i < FB_KIND_COUNT; ++i) {
        uint8_t have = st->inv[i];
        if (!have) continue;
        int32_t dem = st->demand[i];
        if (rain && k_items[i].spoil) dem = dem * 6 / 10;
        if (dem < 1) dem = 1;
        uint8_t sold = (uint8_t)(have < (uint8_t)dem ? have : (uint8_t)dem);
        int32_t rev = (int32_t)sold * k_items[i].price;
        st->inv[i] = (uint8_t)(have - sold);
        st->inv_total = (uint8_t)(st->inv_total - sold);
        revenue += rev;
        if (k_items[i].stamina > max_stamina) max_stamina = k_items[i].stamina;
    }
    st->stamina = (int16_t)clamp32(st->stamina - max_stamina, 0, 100);
    st->cash += revenue;
    st->day_earned += revenue;
    logf_(log, logsz, "摆摊收入 +%d（%s）", (int)revenue,
          rain ? "下雨，食品需求降" : "天气还行");
    return revenue;
}

/* ============================ 其他工作线 ============================ */

bool fb_buy_ebike(fb_state_t *st) {
    int32_t need = FB_EBIKE_COST + FB_EBIKE_GEAR;
    if (st->has_ebike && st->has_gear) return false;
    if (st->cash < need) return false;
    st->cash -= need;
    st->day_spent += need;
    st->has_ebike = 1;
    st->has_gear = 1;
    return true;
}

bool fb_rent_car(fb_state_t *st) {
    if (st->has_car) return false;
    if (st->cash < FB_RIDE_DEPOSIT) return false;
    st->cash -= FB_RIDE_DEPOSIT;
    st->day_spent += FB_RIDE_DEPOSIT;
    st->has_car = 1;
    st->rent_paid_until = (int16_t)(st->day + FB_RIDE_RENT_DAYS);
    return true;
}

bool fb_buy_live_gear(fb_state_t *st) {
    if (st->has_live) return false;
    if (st->cash < FB_LIVE_GEAR) return false;
    st->cash -= FB_LIVE_GEAR;
    st->day_spent += FB_LIVE_GEAR;
    st->has_live = 1;
    return true;
}

int32_t fb_run_delivery(fb_state_t *st, char *log, size_t logsz) {
    if (!log) return 0;
    log[0] = '\0';
    if (!fb_line_unlocked(st, FB_LINE_DELIVERY)) {
        logf_(log, logsz, "还没电动车，跑不了单");
        return 0;
    }
    if (!spend_hours(st, FB_LINE_DELIVERY, 6)) {
        logf_(log, logsz, "今天只剩 %u 小时，跑单要 6 小时",
              (unsigned)fb_hours_left(st));
        return 0;
    }
    int32_t orders = fb_range(st, 22, 46);
    int32_t per = fb_range(st, 6, 9);
    int32_t income = orders * per;
    int32_t fine = 0;
    if (fb_chance(st, 25)) {
        fine = fb_range(st, 3, 8) * 3;   /* 超时/差评扣款 */
    }
    st->stamina = (int16_t)clamp32(st->stamina - 26, 0, 100);
    int32_t net = income - fine;
    st->cash += net;
    st->day_earned += income;
    if (fine) st->cash -= fine;
    logf_(log, logsz, "跑 %d 单，+%d%s", (int)orders, (int)income,
          fine ? "，超时扣款" : "");
    return net;
}

int32_t fb_run_gig(fb_state_t *st, uint8_t gig, char *log, size_t logsz) {
    if (!log) return 0;
    log[0] = '\0';
    if (gig >= FB_GIG_COUNT) return 0;
    if (!spend_hours(st, FB_LINE_GIG, 8)) {
        logf_(log, logsz, "今天只剩 %u 小时，零工要 8 小时",
              (unsigned)fb_hours_left(st));
        return 0;
    }
    bool unpaid = fb_chance(st, 28);
    int32_t wage = unpaid ? 0 : k_gigs[gig].wage;
    st->stamina = (int16_t)clamp32(st->stamina - k_gigs[gig].stamina, 0, 100);
    st->cash += wage;
    st->day_earned += wage;
    logf_(log, logsz, "%s，%s", k_gigs[gig].name,
          unpaid ? "中介拖欠，今天白干" : "日结到账");
    return wage;
}

int32_t fb_run_ride(fb_state_t *st, char *log, size_t logsz) {
    if (!log) return 0;
    log[0] = '\0';
    if (!fb_line_unlocked(st, FB_LINE_RIDE)) {
        logf_(log, logsz, "没租车，跑不了网约车");
        return 0;
    }
    if (!spend_hours(st, FB_LINE_RIDE, 8)) {
        logf_(log, logsz, "今天只剩 %u 小时，跑车要 8 小时",
              (unsigned)fb_hours_left(st));
        return 0;
    }
    int32_t km = fb_range(st, 80, 220);
    int32_t gross = (km * 24) / 10;              /* 2.4 元/公里 */
    int32_t energy = (km * 35) / 100;            /* 0.35 元/公里 */
    int32_t fine = fb_chance(st, 18) ? fb_range(st, 100, 400) : 0;
    int32_t net = gross - energy - fine;
    st->stamina = (int16_t)clamp32(st->stamina - 20, 0, 100);
    st->cash += net;
    st->day_earned += gross;
    logf_(log, logsz, "跑 %d 公里，+%d%s", (int)km, (int)gross,
          fine ? "，吃罚单" : "");
    return net;
}

int32_t fb_run_live(fb_state_t *st, uint8_t topic, char *log, size_t logsz) {
    if (!log) return 0;
    log[0] = '\0';
    if (!fb_line_unlocked(st, FB_LINE_LIVE)) {
        logf_(log, logsz, "没设备，开不了播");
        return 0;
    }
    if (topic >= FB_TOPIC_COUNT) return 0;
    if (!spend_hours(st, FB_LINE_LIVE, 3)) {
        logf_(log, logsz, "今天只剩 %u 小时，开播要 3 小时",
              (unsigned)fb_hours_left(st));
        return 0;
    }
    const fb_topic_t *t = &k_topics[topic];
    if (st->cash < t->cost) {
        logf_(log, logsz, "开播成本 %d 元，现金不够", (int)t->cost);
        return 0;
    }
    st->cash -= t->cost;
    st->day_spent += t->cost;
    st->stamina = (int16_t)clamp32(st->stamina - 30, 0, 100);

    bool flop = fb_chance(st, t->risk_pct);
    int32_t gain_fans = flop ? -(int32_t)(st->fans / 3) : (int32_t)t->fans;
    int32_t fans_new = clamp32((int32_t)st->fans + gain_fans, 0, FB_LIVE_FANS_CAP);
    st->fans = (uint16_t)fans_new;
    /* 礼物收入与粉丝量、选题系数挂钩 */
    int32_t gift = flop ? 0
                        : (int32_t)(st->fans / 10 + 1) * (int32_t)t->gift_pct / 100;
    st->cash += gift;
    st->day_earned += gift;
    if (flop) {
        st->reputation = (int16_t)clamp32(st->reputation - 3, 0, 100);
        logf_(log, logsz, "翻车了，掉粉 %d", (int)-gain_fans);
    } else {
        logf_(log, logsz, "涨粉 %d，礼物 +%d", (int)gain_fans, (int)gift);
    }
    return gift;
}

/* ============================ 日结算 ============================ */

void fb_end_day(fb_state_t *st, char *log, size_t logsz) {
    if (!log) return;
    log[0] = '\0';

    st->cash -= FB_MEAL;
    st->day_spent += FB_MEAL;
    logf_(log, logsz, "吃饭 -%d ", FB_MEAL);

    bool used_vehicle = (st->today_lines & ((1u << FB_LINE_DELIVERY) | (1u << FB_LINE_RIDE))) != 0;
    int32_t commute = used_vehicle ? FB_CHARGE : FB_METRO;
    st->cash -= commute;
    st->day_spent += commute;
    logf_(log, logsz, "交通 -%d ", (int)commute);

    if (st->day % 30 == 0) {
        int32_t fixed = FB_RENT_MONTHLY + FB_PHONE_MONTHLY + FB_UTILITY_MONTHLY;
        st->cash -= fixed;
        st->day_spent += fixed;
        logf_(log, logsz, "房租水电 -%d ", (int)fixed);
    }

    /* 租车月租：到期未付则收车、押金不退 */
    if (st->has_car && st->day >= st->rent_paid_until) {
        if (st->cash >= FB_RIDE_RENT) {
            st->cash -= FB_RIDE_RENT;
            st->day_spent += FB_RIDE_RENT;
            st->rent_paid_until = (int16_t)(st->day + FB_RIDE_RENT_DAYS);
            logf_(log, logsz, "车租 -%d ", FB_RIDE_RENT);
        } else {
            st->has_car = 0;
            st->rent_paid_until = 0;
            logf_(log, logsz, "交不起月租，车被收走 ");
        }
    }

    if (st->debt > 0) {
        int32_t rate = FB_DEBT_MIN_INTEREST;   /* 日息约 1%，但至少 2 元 */
        int32_t interest = st->debt / 100;
        if (interest < rate) interest = rate;
        st->debt += interest;
        logf_(log, logsz, "债息 +%d ", (int)interest);
    }

    st->stamina = (int16_t)clamp32(st->stamina + 30, 0, 100);
    st->health = (int16_t)clamp32(st->health + 8, 0, 100);

    /* 加班透支：工时 >12 后线性升概率，上限 40% */
    if (st->hours_used > 12) {
        uint32_t p = (uint32_t)((st->hours_used - 12) * 40 / 4);
        if (p > 40) p = 40;
        if (fb_chance(st, p)) {
            int32_t oh = fb_range(st, 6, 15);
            st->health = (int16_t)clamp32(st->health - oh, 0, 100);
            logf_(log, logsz, "过劳 -%d 健康 ", (int)oh);
        }
    }
}

void fb_advance_day(fb_state_t *st) {
    if (st->ending != FB_END_NONE) return;
    st->day++;
    st->hours_used = 0;
    st->today_lines = 0;
    st->day_earned = 0;
    st->day_spent = 0;
    fb_regen_day(st);
}

void fb_rest(fb_state_t *st) {
    st->health = (int16_t)clamp32(st->health + 15, 0, 100);
    st->stamina = (int16_t)clamp32(st->stamina + 40, 0, 100);
}

/* ============================ 事件 ============================ */const fb_event_t *fb_roll_event(fb_state_t *st) {
    uint8_t pool[FB_EVENT_POOL];
    uint16_t weight_sum = 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < FB_EVENT_POOL; ++i) {
        const fb_event_t *e = &k_events[i];
        if (e->line_mask && !(st->today_lines & e->line_mask)) continue;
        if (e->once && (st->once_mask & (1u << i))) continue;
        if (i == 5 && st->family_used) continue;   /* ev_parents 用过后不再抽 */
        bool recent = false;
        for (uint8_t r = 0; r < st->recent_n; ++r) {
            if (st->recent[r] == i) { recent = true; break; }
        }
        if (recent) continue;
        pool[n++] = i;
        weight_sum = (uint16_t)(weight_sum + e->weight);
    }
    if (n == 0) return NULL;

    uint32_t r = fb_rand(st) % weight_sum;
    uint8_t hit = pool[n - 1];
    for (uint8_t k = 0; k < n; ++k) {
        const fb_event_t *e = &k_events[pool[k]];
        if (r < e->weight) { hit = pool[k]; break; }
        r -= e->weight;
    }

    /* 去重窗口：记最近 3 条 */
    if (st->recent_n < 3) {
        st->recent[st->recent_n++] = hit;
    } else {
        st->recent[0] = st->recent[1];
        st->recent[1] = st->recent[2];
        st->recent[2] = hit;
    }
    if (k_events[hit].once) st->once_mask |= (uint8_t)(1u << hit);
    return &k_events[hit];
}

void fb_apply_choice(fb_state_t *st, const fb_event_t *ev, uint8_t choice,
                     char *log, size_t logsz) {
    if (!ev) return;
    if (log) log[0] = '\0';
    if (choice >= ev->choice_n) choice = 0;
    const fb_choice_t *c = &ev->choices[choice];

    st->cash += c->cash;
    if (c->cash > 0) st->day_earned += c->cash;
    if (c->health) st->health = (int16_t)clamp32(st->health + c->health, 0, 100);
    if (c->stamina) st->stamina = (int16_t)clamp32(st->stamina + c->stamina, 0, 100);
    if (c->rep) st->reputation = (int16_t)clamp32(st->reputation + c->rep, 0, 100);
    if (c->fans) {
        int32_t f = clamp32((int32_t)st->fans + c->fans, 0, FB_LIVE_FANS_CAP);
        st->fans = (uint16_t)f;
    }

    switch (c->special) {
        case 1:   /* 第二条命：父母支援，清空负债 */
            if (!st->family_used) {
                st->family_used = 1;
                st->cash += FB_FAMILY_CASH;
                st->day_earned += FB_FAMILY_CASH;
                st->debt = 0;
                st->health = (int16_t)clamp32(st->health + FB_FAMILY_HEALTH, 0, 100);
                st->reputation = (int16_t)clamp32(st->reputation + FB_FAMILY_REP, 0, 100);
                logf_(log, logsz, "父母支援 +%d，债清了", FB_FAMILY_CASH);
            }
            break;
        case 2:   /* 赌一把：五五开 */
            if (fb_chance(st, 50)) {
                int32_t f = clamp32((int32_t)st->fans + 80, 0, FB_LIVE_FANS_CAP);
                st->fans = (uint16_t)f;
                logf_(log, logsz, "赌赢了，涨粉 80");
            } else {
                int32_t lost = (int32_t)(st->fans / 2);
                st->fans = (uint16_t)(st->fans - lost);
                st->reputation = (int16_t)clamp32(st->reputation - 4, 0, 100);
                logf_(log, logsz, "赌输了，掉粉 %d", (int)lost);
            }
            break;
        case 3:   /* 掉粉四成 */
            st->fans = (uint16_t)(st->fans - st->fans * 4 / 10);
            break;
        default:
            break;
    }
}

void fb_skip_event(fb_state_t *st) { (void)st; }

/* ============================ 结束判定 ============================ */

bool fb_check_end(fb_state_t *st) {
    if (st->ending != FB_END_NONE) return true;
    if (st->health <= 0) {
        st->ending = FB_END_DEAD;
        return true;
    }
    if (st->day > FB_TOTAL_DAYS) {
        int32_t nw = fb_net_worth(st);
        if (nw >= 45000) st->ending = FB_END_RICH;
        else if (nw >= 12000) st->ending = FB_END_SETTLE;
        else if (nw >= 0) st->ending = FB_END_ALIVE;
        else if (nw >= -5000) st->ending = FB_END_HOME;
        else st->ending = FB_END_HOMELESS;
        return true;
    }
    if (st->cash < FB_CASH_BANKRUPT && fb_net_worth(st) < FB_CASH_BANKRUPT) {
        st->ending = FB_END_HOMELESS;
        return true;
    }
    return false;
}
