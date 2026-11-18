/* 折叠北京 · 名声判定契约测试（host）。
 *
 * 玩家看不懂「点位 ×130%」之后顺手排查，发现同类问题更严重的一处：
 * 事件里两个选项写「看名声」，代码却从不读名声、无条件免罚；而名声
 * 本身只被写入、从不参与任何判定，结局也不看它 —— 一个纯装饰属性
 * 却被拿来当决策依据，玩家所有的权衡都是假的。
 *
 * 这里锁四件事：
 *   1. 名声满 50 求情免罚（退款、现金不变）
 *   2. 名声不够照罚（且文案里的金额与实际扣款一致）
 *   3. 普通选项增减名声要写进结果行，不能悄悄改
 *   4. 赌输的「名声-4」隐藏代价必须在日志里说出来
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fb_game.h"

static const fb_event_t *ev_by_title(const char *title) {
    size_t n = 0;
    const fb_event_t *t = fb_event_table(&n);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(t[i].title, title) == 0) return &t[i];
    }
    return NULL;
}

static void test_plea(void) {
    const fb_event_t *chk = ev_by_title("路口查车");
    assert(chk && chk->choice_n == 2);
    /* 求情是第二项：带罚款、special=4，判定门槛写在文案里 */
    assert(chk->choices[1].special == 4);
    assert(chk->choices[1].cash == -200);

    char log[FB_LOG_SIZE];
    fb_state_t st;

    /* 名声够：先扣的 200 要退回来 */
    fb_init(&st, 0, 1u);
    st.cash = 1000;
    st.reputation = 55;
    fb_apply_choice(&st, chk, 1, log, sizeof log);
    assert(st.cash == 1000);
    assert(strstr(log, "免罚") != NULL);

    /* 名声不够：照罚，且日志写明罚了多少 */
    fb_init(&st, 0, 1u);
    st.cash = 1000;
    st.reputation = 40;
    fb_apply_choice(&st, chk, 1, log, sizeof log);
    assert(st.cash == 800);
    assert(strstr(log, "照罚 200") != NULL);

    /* 恰好 50 算够 */
    fb_init(&st, 0, 1u);
    st.cash = 1000;
    st.reputation = 50;
    fb_apply_choice(&st, chk, 1, log, sizeof log);
    assert(st.cash == 1000);
    printf("  plea OK\n");
}

static void test_rep_in_log(void) {
    const fb_event_t *wallet = ev_by_title("捡到个钱包");
    assert(wallet);
    char log[FB_LOG_SIZE];
    fb_state_t st;

    /* 拿走钱包：-2 名声，结果行原本是空的，现在必须写出来 */
    fb_init(&st, 0, 1u);
    st.reputation = 55;
    st.cash = 500;
    fb_apply_choice(&st, wallet, 0, log, sizeof log);
    assert(st.cash == 800);
    assert(st.reputation == 53);
    assert(strstr(log, "名声-2") != NULL);

    /* 交岗亭：+3 名声 */
    fb_init(&st, 0, 1u);
    st.reputation = 55;
    fb_apply_choice(&st, wallet, 1, log, sizeof log);
    assert(st.reputation == 58);
    assert(strstr(log, "名声+3") != NULL);
    printf("  rep_in_log OK\n");
}

static void test_gamble_penalty_visible(void) {
    const fb_event_t *report = ev_by_title("视频被举报");
    assert(report && report->choice_n == 2);
    assert(report->choices[0].special == 3);   /* 删了掉粉四成 */
    assert(report->choices[1].special == 2);   /* 硬刚五五开 */

    char log[FB_LOG_SIZE];
    fb_state_t st;
    bool saw_loss = false, saw_win = false;
    /* 五五开：遍历种子找出输赢两条分支，两条都要有明确结果文案 */
    for (uint32_t seed = 1; seed < 64 && !(saw_loss && saw_win); ++seed) {
        fb_init(&st, 0, seed);
        st.reputation = 55;
        st.fans = 100;
        fb_apply_choice(&st, report, 1, log, sizeof log);
        if (strstr(log, "赌输了")) {
            saw_loss = true;
            assert(strstr(log, "名声-4") != NULL);  /* 这条代价原先没写 */
        }
        if (strstr(log, "赌赢了")) saw_win = true;
    }
    assert(saw_loss && saw_win);
    printf("  gamble_penalty OK\n");
}

static void test_copy_matches_engine(void) {
    /* 文案写「名声满50免罚」，门槛就必须是 50；改数值时测试会挡住。 */
    const fb_event_t *ride = ev_by_title("运管查车");
    assert(ride && ride->choices[1].special == 4);
    assert(ride->choices[1].cash == -260);

    char log[FB_LOG_SIZE];
    fb_state_t st;
    fb_init(&st, 0, 1u);
    st.cash = 1000;
    st.reputation = 50;
    fb_apply_choice(&st, ride, 1, log, sizeof log);
    assert(st.cash == 1000);
    assert(strstr(log, "免罚") != NULL);
    printf("  copy_matches OK\n");
}

int main(void) {
    test_plea();
    test_rep_in_log();
    test_gamble_penalty_visible();
    test_copy_matches_engine();
    puts("名声判定与文案契约: PASS");
    return 0;
}
