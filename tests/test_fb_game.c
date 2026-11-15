/* 折叠北京 · 生存引擎契约测试（host）。
 * 断言的是"可观察行为"而非内部实现：初值、确定性、工时门槛、
 * 结算扣费、事件增量、结局分档。任何一条改动都应让它先红。
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fb_game.h"

static void test_init(void) {
    fb_state_t st;
    fb_init(&st, 0, 12345u);
    assert(st.day == 1);
    assert(st.cash == 1600);
    assert(st.health == 90);
    assert(st.stamina == 100);
    assert(st.reputation == 55);
    assert(st.storage == 16);
    assert(st.ending == FB_END_NONE);
    assert(fb_hours_left(&st) == FB_DAY_HOURS);
    assert(fb_sleep_hours(&st) == FB_DAY_HOURS);   /* 第一天还没干活，能睡满 */
    assert(st.sleep_debt == 0);
    assert(st.drowsy == FB_DROWSY_NONE);

    fb_init(&st, 1, 1u);
    assert(st.cash == 2600);
    printf("  init OK\n");
}

static void test_determinism(void) {
    fb_state_t a, b;
    fb_init(&a, 0, 777u);
    fb_init(&b, 0, 777u);
    for (int i = 0; i < 50; ++i) {
        assert(fb_rand(&a) == fb_rand(&b));
    }
    /* 不同种子必须不同序列 */
    fb_init(&b, 0, 778u);
    assert(fb_rand(&a) != fb_rand(&b));
    printf("  determinism OK\n");
}

static void test_hours_gate(void) {
    fb_state_t st;
    fb_init(&st, 0, 42u);
    /* 日结要 8 小时：24 小时够干三份，第四份被拒 —— 一天能打多份工，
     * 但总时长就是 24 小时，一分不多。 */
    char log[FB_LOG_SIZE];
    for (int i = 0; i < 3; ++i) {
        assert(fb_run_gig(&st, 0, log, sizeof log) >= 0);
    }
    assert(st.hours_used == 24);
    assert(fb_hours_left(&st) == 0);
    assert(fb_sleep_hours(&st) == 0);      /* 干满 24 小时 = 今晚没得睡 */
    int32_t before = st.cash;
    fb_run_gig(&st, 0, log, sizeof log);
    assert(st.cash == before);             /* 干不动，不产生收入 */
    assert(strstr(log, "小时") != NULL);
    printf("  hours gate OK\n");
}

/* 睡眠偿还：干得越多睡得越少，健康掉得越狠；连着缺觉累积睡眠债并把人拖成瞌睡 */
static void test_sleep_curve(void) {
    char log[FB_LOG_SIZE];

    /* ① 干 6 小时（睡 18）→ 睡够，健康回血 */
    fb_state_t a;
    fb_init(&a, 0, 100u);
    a.has_ebike = 1; a.has_gear = 1;
    a.health = 60;
    fb_run_delivery(&a, log, sizeof log);
    int16_t after_work = a.health;
    fb_end_day(&a, log, sizeof log);
    assert(a.last_sleep == 18);
    assert(a.health > after_work);         /* 睡够了，当晚回血 */
    assert(a.drowsy == FB_DROWSY_NONE);

    /* ② 干满 24 小时（睡 0）→ 通宵，健康暴跌 + 立即欠一天觉 */
    fb_state_t b;
    fb_init(&b, 0, 100u);
    b.has_ebike = 1; b.has_gear = 1; b.has_car = 1; b.has_live = 1;
    b.health = 90;
    fb_run_gig(&b, 1, log, sizeof log);        /* 8 */
    fb_run_ride(&b, log, sizeof log);          /* 8 */
    fb_run_live(&b, 0, log, sizeof log);       /* 3 */
    fb_buy_item(&b, 0, 5);
    fb_run_market(&b, log, sizeof log);        /* 5 → 共 24 */
    assert(b.hours_used == 24);
    fb_end_day(&b, log, sizeof log);
    assert(b.last_sleep == 0);
    assert(b.sleep_debt == 1);
    assert(b.health < 60);                 /* 通宵一夜，健康掉到底 */
    assert(b.drowsy >= FB_DROWSY_TIRED);   /* 第二天一定犯困 */

    printf("  sleep curve OK (通宵后 health=%d drowsy=%u)\n", b.health,
           b.drowsy);
}

/* 连续多日不睡：睡眠债累加、瞌睡升到「撑不住」、晕倒概率打开。
 * 每天干 19 小时只睡 5 小时（欠觉但还活着），四天下来债满 4 天、白天撑不住。 */
static void test_sleep_debt_chain(void) {
    char log[FB_LOG_SIZE];
    fb_state_t st;
    fb_init(&st, 0, 2026u);
    st.has_live = 1;
    st.health = 100;

    for (int d = 0; d < 3; ++d) {
        fb_run_gig(&st, 2, log, sizeof log);   /* 发传单 8h，最省体力 */
        fb_run_gig(&st, 2, log, sizeof log);   /* 再 8h */
        fb_run_live(&st, 0, log, sizeof log);  /* 开播 3h → 共 19 */
        assert(st.hours_used == 19);
        assert(fb_sleep_hours(&st) == 5);
        fb_end_day(&st, log, sizeof log);
        assert(st.last_sleep == 5);
        assert(st.sleep_debt == (uint8_t)(d + 1));
        assert(st.ending == FB_END_NONE);
        fb_advance_day(&st);
        assert(!fb_check_end(&st));
    }
    /* 连欠三天：债记到 3，白天已经犯困，人还没倒 */
    assert(st.sleep_debt == 3);
    assert(st.drowsy >= FB_DROWSY_TIRED);

    /* 债务拖到第 4 天 → 等级升到「撑不住」（这一步可能顺带晕倒，所以只看等级） */
    st.sleep_debt = 4;
    fb_run_gig(&st, 2, log, sizeof log);
    fb_run_gig(&st, 2, log, sizeof log);
    fb_run_live(&st, 0, log, sizeof log);
    assert(fb_sleep_hours(&st) == 5);
    fb_end_day(&st, log, sizeof log);
    assert(st.drowsy == FB_DROWSY_CRITICAL);
    printf("  sleep debt chain OK (debt=%u drowsy=%u health=%d faint=%u)\n",
           st.sleep_debt, st.drowsy, st.health, st.faint_cnt);
}

static void test_quotes(void) {
    /* 两个池子各 18 条，开场按天奇偶交替，日终按盈亏换语气 */
    assert(fb_quote_count(FB_QUOTE_SANG) == 18);
    assert(fb_quote_count(FB_QUOTE_POISON) == 18);
    for (int16_t d = 1; d <= 100; ++d) {
        const char *banner = fb_quote_daily(d, 0, 0);
        assert(banner && banner[0]);
        uint8_t k = fb_quote_daily_kind(d, 0, 0);
        assert(k == ((d % 2 == 0) ? FB_QUOTE_SANG : FB_QUOTE_POISON));
        /* 同一天的两句不能撞 */
        uint8_t ek = fb_quote_daily_kind(d, 1, -500);
        const char *end = fb_quote_daily(d, 1, -500);
        assert(ek == FB_QUOTE_SANG);       /* 亏钱那天配丧的 */
        assert(strcmp(banner, end) != 0);
    }
    /* 挣了钱的日子，日终给毒鸡汤 */
    assert(fb_quote_daily_kind(7, 1, 800) == FB_QUOTE_POISON);
    printf("  quotes OK\n");
}

static void test_market(void) {
    fb_state_t st;
    fb_init(&st, 0, 99u);
    assert(fb_buy_item(&st, 0, 16));    /* 满仓烤冷面 */
    assert(st.inv_total == 16);
    /* 仓储满后再进必须失败 */
    assert(!fb_buy_item(&st, 0, 1));
    char log[FB_LOG_SIZE];
    int32_t rev = fb_run_market(&st, log, sizeof log);
    assert(rev > 0);
    assert(st.cash > 1600 - 16 * 4);
    printf("  market OK (revenue=%d)\n", (int)rev);
}

static void test_endday_fees(void) {
    fb_state_t st;
    fb_init(&st, 0, 7u);
    int32_t cash0 = st.cash;
    char log[FB_LOG_SIZE];
    fb_end_day(&st, log, sizeof log);
    /* 吃饭 18 + 地铁 4 = 22 */
    assert(st.cash == cash0 - 22);
    assert(st.health >= 90);        /* 一天没干活 = 睡满 24 小时，健康回血 */
    assert(st.last_sleep == FB_DAY_HOURS);
    printf("  endday fees OK\n");
}

static void test_debt_interest(void) {
    fb_state_t st;
    fb_init(&st, 0, 5u);
    assert(fb_loan(&st, 1000));
    assert(st.debt == 1000);
    char log[FB_LOG_SIZE];
    fb_end_day(&st, log, sizeof log);
    assert(st.debt > 1000);         /* 滚了利息 */
    printf("  debt interest OK (debt=%d)\n", (int)st.debt);
}

static void test_event_apply(void) {
    fb_state_t st;
    fb_init(&st, 0, 3u);
    /* 手工构造一个"捡钱包"式增量，验证字段累加与 clamp */
    size_t n = 0;
    const fb_event_t *evs = fb_event_table(&n);
    assert(n == FB_EVENT_POOL);
    int16_t hp0 = st.health;
    fb_apply_choice(&st, &evs[0], 1, NULL, 0);   /* 看门诊：-500 现金 +6 健康 */
    assert(st.cash == 1600 - 500);
    assert(st.health == hp0 + 6);
    /* 健康不得越界 */
    st.health = 99;
    fb_apply_choice(&st, &evs[6], 0, NULL, 0);   /* 大爷塞药 +4 */
    assert(st.health == 100);
    printf("  event apply OK\n");
}

static void test_endings(void) {
    fb_state_t st;
    fb_init(&st, 0, 11u);
    /* 健康归零 → 病倒 */
    st.health = 0;
    assert(fb_check_end(&st));
    assert(st.ending == FB_END_DEAD);

    fb_init(&st, 0, 12u);
    st.day = FB_TOTAL_DAYS + 1;
    st.cash = 50000;
    st.debt = 0;
    assert(fb_check_end(&st));
    assert(st.ending == FB_END_RICH);

    fb_init(&st, 0, 13u);
    st.day = FB_TOTAL_DAYS + 1;
    st.cash = -6000;
    assert(fb_check_end(&st));
    assert(st.ending == FB_END_HOMELESS);
    printf("  endings OK\n");
}

static void test_full_run(void) {
    fb_state_t st;
    fb_init(&st, 1, 20261115u);
    char log[FB_LOG_SIZE];
    int guard = 0;
    while (!fb_check_end(&st) && guard++ < 400) {
        while (st.inv_total < st.storage && fb_buy_item(&st, 0, 1)) {}
        fb_run_market(&st, log, sizeof log);
        /* 一天能接着干第二份工 —— 但留 6 小时睡觉，不然连轴转必垮 */
        if (fb_hours_left(&st) >= 8) fb_run_gig(&st, 1, log, sizeof log);
        assert(st.hours_used <= FB_DAY_HOURS);
        fb_end_day(&st, log, sizeof log);
        const fb_event_t *e = fb_roll_event(&st);
        if (e) fb_apply_choice(&st, e, 0, log, sizeof log);
        fb_advance_day(&st);
    }
    assert(fb_check_end(&st));
    assert(st.ending != FB_END_NONE);
    assert(st.day <= FB_TOTAL_DAYS + 1);
    printf("  full run OK (day=%d ending=%u net=%d debt=%u faint=%u)\n", st.day,
           st.ending, (int)fb_net_worth(&st), st.sleep_debt, st.faint_cnt);
}

int main(void) {
    printf("test_fb_game\n");
    test_init();
    test_determinism();
    test_hours_gate();
    test_sleep_curve();
    test_sleep_debt_chain();
    test_quotes();
    test_market();
    test_endday_fees();
    test_debt_interest();
    test_event_apply();
    test_endings();
    test_full_run();
    printf("test_fb_game: PASS\n");
    return 0;
}
