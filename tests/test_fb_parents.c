/* 折叠北京 · 父母支援事件契约测试（host）。
 *
 * 玩家反馈：「突发.件，三十来天，选择接听父母电话给2200 没有到账了」。
 *
 * 根因是事件过滤写死了下标：fb_roll_event 里 `if (i == 5 && family_used)`，
 * 而表里 i==5 是「刮刮乐中五十」，父母事件排在 i==4。后果是这条用过一次
 * 之后仍会被反复抽到 —— 玩家第二次选「接电话」，结算里 family_used 已经
 * 置 1，整段静默跳过：一分钱不给，结果行也是空的（fb_app 里 log 为空就
 * 不更新 toast），屏幕上什么反馈都没有。
 *
 * 这里锁四件事：
 *   1. 父母事件靠 special==1 识别，不靠下标（下标挪一下就会错位）
 *   2. family_used 之后抽不到这条事件
 *   3. family_used 之前抽得到（别把过滤修成永远不出现）
 *   4. 万一还是进来了，必须给结果文案，不许静默
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fb_game.h"

#define PARENTS_TITLE "妈打来电话"

static const fb_event_t *ev_by_title(const char *title) {
    size_t n = 0;
    const fb_event_t *t = fb_event_table(&n);
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(t[i].title, title) == 0) return &t[i];
    }
    return NULL;
}

/* 反复抽签找一条指定事件。recent_n 每次清零，否则去重窗口会把它挡掉。 */
static bool roll_finds(const char *title, uint8_t family_used, int rounds) {
    fb_state_t st;
    fb_init(&st, 0, 1u);
    st.family_used = family_used;
    for (int i = 0; i < rounds; ++i) {
        st.recent_n = 0;
        const fb_event_t *e = fb_roll_event(&st);
        if (!e) return false;
        if (strcmp(e->title, title) == 0) return true;
    }
    return false;
}

static void test_identified_by_special(void) {
    const fb_event_t *p = ev_by_title(PARENTS_TITLE);
    assert(p && p->choice_n == 2);
    /* 识别标记必须存在：过滤逻辑靠它，靠下标就会在表顺序变化时错位 */
    assert(p->choices[0].special == 1);
    /* 文案承诺 2200，实际到账也必须是 2200 */
    assert(strstr(p->choices[0].label, "2200") != NULL);
    printf("  identified_by_special OK\n");
}

static void test_not_rolled_after_used(void) {
    /* family_used=1：400 次抽签一次都不许出这条 */
    assert(!roll_finds(PARENTS_TITLE, 1, 400));
    printf("  not_rolled_after_used OK\n");
}

static void test_rolled_before_used(void) {
    /* 反过来也要成立，别把过滤修成永远抽不到 */
    assert(roll_finds(PARENTS_TITLE, 0, 400));
    printf("  rolled_before_used OK\n");
}

static void test_payout_once(void) {
    const fb_event_t *p = ev_by_title(PARENTS_TITLE);
    assert(p);
    char log[FB_LOG_SIZE];
    fb_state_t st;

    /* 第一次：2200 必须到账 */
    fb_init(&st, 0, 1u);
    st.cash = 500;
    st.family_used = 0;
    fb_apply_choice(&st, p, 0, log, sizeof log);
    assert(st.family_used == 1);
    assert(st.cash == 500 + 2200);
    assert(strstr(log, "父母支援") != NULL);

    /* 第二次（正常流程抽不到，但存档异常时可能撞上）：钱不再给，
     * 也不能一声不吭 —— 结果行必须有话说 */
    const int32_t before = st.cash;
    log[0] = '\0';
    fb_apply_choice(&st, p, 0, log, sizeof log);
    assert(st.cash == before);
    assert(log[0] != '\0');
    printf("  payout_once OK\n");
}

static void test_decline_keeps_chance(void) {
    /* 选「先不接」不消耗这次机会：还能再接到电话 */
    const fb_event_t *p = ev_by_title(PARENTS_TITLE);
    assert(p);
    char log[FB_LOG_SIZE];
    fb_state_t st;
    fb_init(&st, 0, 1u);
    st.cash = 500;
    fb_apply_choice(&st, p, 1, log, sizeof log);
    assert(st.family_used == 0);
    assert(st.cash == 500);
    assert(roll_finds(PARENTS_TITLE, st.family_used, 400));
    printf("  decline_keeps_chance OK\n");
}

int main(void) {
    test_identified_by_special();
    test_not_rolled_after_used();
    test_rolled_before_used();
    test_payout_once();
    test_decline_keeps_chance();
    puts("父母支援事件契约: PASS");
    return 0;
}
