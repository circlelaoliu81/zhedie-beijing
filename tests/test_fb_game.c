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
    assert(fb_hours_left(&st) == 16);

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
    /* 日结要 8 小时：连做两次应耗尽 16 小时，第三次被拒 */
    char log[FB_LOG_SIZE];
    fb_run_gig(&st, 0, log, sizeof log);
    fb_run_gig(&st, 0, log, sizeof log);
    assert(fb_hours_left(&st) == 0);
    int32_t before = st.cash;
    fb_run_gig(&st, 0, log, sizeof log);
    assert(st.cash == before);      /* 干不动，不产生收入 */
    assert(strstr(log, "小时") != NULL);
    printf("  hours gate OK\n");
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
    assert(st.health >= 90);        /* 健康有回复 */
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
        if (fb_hours_left(&st) >= 8) fb_run_gig(&st, 1, log, sizeof log);
        fb_end_day(&st, log, sizeof log);
        const fb_event_t *e = fb_roll_event(&st);
        if (e) fb_apply_choice(&st, e, 0, log, sizeof log);
        fb_advance_day(&st);
    }
    assert(fb_check_end(&st));
    assert(st.ending != FB_END_NONE);
    assert(st.day <= FB_TOTAL_DAYS + 1);
    printf("  full run OK (day=%d ending=%u net=%d)\n", st.day, st.ending,
           (int)fb_net_worth(&st));
}

int main(void) {
    printf("test_fb_game\n");
    test_init();
    test_determinism();
    test_hours_gate();
    test_market();
    test_endday_fees();
    test_debt_interest();
    test_event_apply();
    test_endings();
    test_full_run();
    printf("test_fb_game: PASS\n");
    return 0;
}
