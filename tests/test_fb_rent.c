/* 折叠北京 · 租车月租契约测试（host）。
 *
 * 玩家反馈「网约车莫名其妙消失」：车被收走本身是设计，但收车那段文案
 * 排在吃饭/交通/房租之后，而收工页明细只有一行 17 个字宽 —— 屏幕上
 * 从来没出现过「车被收走」，玩家只看到押金没了、行当变灰。
 *
 * 这里锁三件事：
 *   1. 剩余天数算得对（行当页的「剩 N 天」靠它）
 *   2. 收车/续租必须出现在结算文案最前面
 *   3. 月末房租不许把够交的车租挤掉（工具优先于住处）
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fb_game.h"

/* 结算里有过劳/晕倒这类随机分支，用「没干活 + 不犯困 + 不欠觉」把它们关掉 */
static void quiet_day(fb_state_t *st) {
    st->hours_used = 0;
    st->drowsy = FB_DROWSY_NONE;
    st->sleep_debt = 0;
    st->health = 90;
    st->stamina = 100;
}

static void test_days_left(void) {
    fb_state_t st;
    fb_init(&st, 0, 1u);
    assert(fb_ride_days_left(&st) == 0);          /* 没车 */

    st.has_car = 1;
    st.day = 12;
    st.rent_paid_until = 42;
    assert(fb_ride_days_left(&st) == 30);

    st.rent_paid_until = 13;
    assert(fb_ride_days_left(&st) == 1);

    /* 到期当天已经算 0：再晚就是收车，不能显示「还剩 0 天」还让人开 */
    st.rent_paid_until = 12;
    assert(fb_ride_days_left(&st) == 0);
    st.rent_paid_until = 5;                        /* 已经过期 */
    assert(fb_ride_days_left(&st) == 0);
    printf("  days_left OK\n");
}

static void test_renew_before_due(void) {
    fb_state_t st;
    fb_init(&st, 0, 2u);
    char log[FB_LOG_SIZE];

    quiet_day(&st);
    st.has_car = 1;
    st.day = 10;
    st.rent_paid_until = 40;
    st.cash = 5000;
    const int32_t before = st.cash;
    fb_end_day(&st, log, sizeof log);
    assert(st.has_car == 1);
    assert(st.rent_paid_until == 40);              /* 没到期，不动 */
    const int32_t spent = before - st.cash;        /* 只该有饭钱和交通 */
    assert(spent > 0 && spent < 100);
    assert(strstr(log, "车租") == NULL);
    printf("  renew_before_due OK\n");
}

static void test_renew_on_due(void) {
    fb_state_t st;
    fb_init(&st, 0, 3u);
    char log[FB_LOG_SIZE];

    quiet_day(&st);
    st.has_car = 1;
    st.day = 40;
    st.rent_paid_until = 40;
    st.cash = 5000;
    fb_end_day(&st, log, sizeof log);

    assert(st.has_car == 1);
    assert(st.rent_paid_until == 70);              /* 续满 30 天 */
    /* 续租是笔大钱，必须排在最前面，而不是被 17 个字宽切掉 */
    assert(strncmp(log, "续车租", strlen("续车租")) == 0);
    printf("  renew_on_due OK\n");
}

static void test_car_seized(void) {
    fb_state_t st;
    fb_init(&st, 0, 4u);
    char log[FB_LOG_SIZE];

    quiet_day(&st);
    st.has_car = 1;
    st.day = 40;
    st.rent_paid_until = 40;
    st.cash = 1000;                                /* 交不起 2800 */
    fb_end_day(&st, log, sizeof log);

    assert(st.has_car == 0);
    assert(st.rent_paid_until == 0);
    /* 核心断言：文案第一个字就是收车，玩家不可能看不见 */
    assert(strncmp(log, "交不起车租", strlen("交不起车租")) == 0);
    /* 押金不退这件事必须写出来，否则就是「钱也没了车也没了」 */
    assert(strstr(log, "押金不退") != NULL);
    printf("  car_seized OK\n");
}

static void test_rent_priority_over_monthly(void) {
    fb_state_t st;
    fb_init(&st, 0, 5u);
    char log[FB_LOG_SIZE];

    /* 第 30 天：车租和房租水电撞在同一天。
     * 旧顺序先扣房租 2059，4700 → 2641，不够 2800，车当场被收走；
     * 新顺序先交车租，车保住 —— 差的就是这一笔。 */
    quiet_day(&st);
    st.has_car = 1;
    st.day = 30;
    st.rent_paid_until = 30;
    st.cash = 4700;
    fb_end_day(&st, log, sizeof log);

    assert(st.has_car == 1);
    assert(st.rent_paid_until == 60);
    assert(strncmp(log, "续车租", strlen("续车租")) == 0);
    printf("  rent_priority_over_monthly OK\n");
}

int main(void) {
    printf("租车月租\n");
    test_days_left();
    test_renew_before_due();
    test_renew_on_due();
    test_car_seized();
    test_rent_priority_over_monthly();
    printf("全部通过\n");
    return 0;
}
