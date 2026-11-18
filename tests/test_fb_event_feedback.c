/* 折叠北京 · 事件结果反馈契约测试（host）。
 *
 * 玩家反馈「父母支援 2200 没到账」，根因是一处写死的下标；顺着这条线
 * 又查出更普遍的一类：**事件选完没有任何回音**。
 *
 *   1. 纯数值选项（认罚 -250、挂急诊 -600、换屏 -350……）在
 *      fb_apply_choice 里只改数值，从来不写结果日志；
 *   2. 就算写了，fb_app 的 FB_SCR_EVENT 分支是先写 toast 再调
 *      advance_after_event，而后者一进来就把 toast 清零 ——
 *      刚写完就被擦掉，结果一个月里从来没上过屏。
 *
 * 这里的契约：任何「会改变状态的选项」都必须产出非空结果文案。
 * 以后加新事件，忘了写日志就直接红。
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fb_game.h"

/* 选项是不是真的什么都没做（「先不接」「守着，认了」这类）。
 * special 非 0 的选项一定会改变状态，不算空操作。 */
static bool choice_is_noop(const fb_choice_t *c) {
    if (c->special != 0) return false;
    return c->cash == 0 && c->health == 0 && c->stamina == 0 &&
           c->rep == 0 && c->fans == 0;
}

static void test_every_choice_has_feedback(void) {
    size_t n = 0;
    const fb_event_t *t = fb_event_table(&n);
    assert(n > 0);

    int silent = 0;
    for (size_t i = 0; i < n; ++i) {
        for (uint8_t k = 0; k < t[i].choice_n; ++k) {
            const fb_choice_t *c = &t[i].choices[k];
            if (choice_is_noop(c)) continue;

            fb_state_t st;
            fb_init(&st, 0, 1u);
            st.cash = 5000;   /* 保证扣得动，别让余额影响文案 */
            st.fans = 300;
            st.reputation = 60;

            char log[FB_LOG_SIZE];
            fb_apply_choice(&st, &t[i], k, log, sizeof log);
            if (log[0] == '\0') {
                printf("  FAIL 静默选项: 「%s」/「%s」\n", t[i].title, c->label);
                ++silent;
            }
        }
    }
    assert(silent == 0);
    printf("  every_choice_has_feedback OK\n");
}

/* special=3 掉粉四成：粉丝数变了就必须报出掉多少，不能只靠玩家自己数 */
static void test_fans_loss_reported(void) {
    size_t n = 0;
    const fb_event_t *t = fb_event_table(&n);
    const fb_event_t *report = NULL;
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(t[i].title, "视频被举报") == 0) { report = &t[i]; break; }
    }
    assert(report);

    fb_state_t st;
    fb_init(&st, 0, 1u);
    st.fans = 1000;
    char log[FB_LOG_SIZE];
    fb_apply_choice(&st, report, 0, log, sizeof log);   /* 删了息事，掉粉四成 */
    assert(st.fans == 600);
    assert(strstr(log, "掉粉 400") != NULL);
    printf("  fans_loss_reported OK\n");
}

/* once_mask 是 uint8_t（8 位）：给下标 >= 8 的事件标 once，
 * (uint8_t)(1u << i) 会直接被截掉，那个事件就永远「只用一次」失效。
 * 现在还没有这种事件，这条测试是为了别在以后悄悄踩进去。 */
static void test_once_mask_fits(void) {
    size_t n = 0;
    const fb_event_t *t = fb_event_table(&n);
    for (size_t i = 0; i < n; ++i) {
        if (!t[i].once) continue;
        if (i >= 8) {
            printf("  FAIL once 事件「%s」下标 %zu >= 8，once_mask 存不下\n",
                   t[i].title, i);
            assert(0);
        }
    }
    printf("  once_mask_fits OK\n");
}

/* 事件页的卡片区高度只够三张（三张时压到 26px）。选项写多了不会报错，
 * 只会第四张画到屏幕外面去 —— 玩家看到一个点不到的选项。 */
static void test_choice_count_fits_layout(void) {
    size_t n = 0;
    const fb_event_t *t = fb_event_table(&n);
    for (size_t i = 0; i < n; ++i) {
        if (t[i].choice_n < 1 || t[i].choice_n > 3) {
            printf("  FAIL 事件「%s」有 %u 个选项，事件页最多排三张\n",
                   t[i].title, (unsigned)t[i].choice_n);
            assert(0);
        }
    }
    printf("  choice_count_fits_layout OK\n");
}

int main(void) {
    test_every_choice_has_feedback();
    test_fans_loss_reported();
    test_once_mask_fits();
    test_choice_count_fits_layout();
    puts("事件结果反馈契约: PASS");
    return 0;
}
