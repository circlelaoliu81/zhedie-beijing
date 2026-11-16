/* 折叠北京 · 空闲息屏契约测试（host）
 *
 * 守四件事：
 *   1. 60 秒整点熄屏，59.9 秒不熄；熄屏信号只发一次（不会每轮都去关背光）
 *   2. 熄屏后的第一下按键只唤醒、被吞掉；唤醒后重新计时，不会立刻又黑
 *   3. 亮屏时按键不吞（否则每 60 秒就吃掉玩家一次操作）
 *   4. 毫秒计数回绕（esp_timer 约 49 天一次）不能误判成「很久没按」
 */
#include <stdio.h>

#include "fb_idle.h"

static uint32_t s_fail;

#define EXPECT(cond, msg)                                      \
    do {                                                       \
        if (!(cond)) {                                         \
            printf("  FAIL %s (line %d)\n", (msg), __LINE__);   \
            ++s_fail;                                          \
        }                                                      \
    } while (0)

static void test_timeout(void) {
    fb_idle_t idle;
    fb_idle_init(&idle, 1000u);
    EXPECT(!fb_idle_is_asleep(&idle), "刚开机不该是熄屏");

    EXPECT(!fb_idle_poll(&idle, 1000u + FB_IDLE_TIMEOUT_MS - 100u),
           "差 100ms 不该熄屏");
    EXPECT(!fb_idle_is_asleep(&idle), "差 100ms 背光应仍亮");

    EXPECT(fb_idle_poll(&idle, 1000u + FB_IDLE_TIMEOUT_MS), "满 60 秒应熄屏");
    EXPECT(fb_idle_is_asleep(&idle), "熄屏后状态应为睡");

    EXPECT(!fb_idle_poll(&idle, 1000u + FB_IDLE_TIMEOUT_MS + 5000u),
           "熄屏信号只发一次");
    printf("  timeout OK\n");
}

static void test_wake(void) {
    fb_idle_t idle;
    fb_idle_init(&idle, 0u);
    (void)fb_idle_poll(&idle, FB_IDLE_TIMEOUT_MS);
    EXPECT(fb_idle_is_asleep(&idle), "先确认已熄屏");

    /* 熄屏下按任何键：这一下只负责点亮，必须被吞掉 */
    EXPECT(fb_idle_on_key(&idle, FB_IDLE_TIMEOUT_MS + 1000u),
           "熄屏后首次按键应被吞");
    EXPECT(!fb_idle_is_asleep(&idle), "按键后应唤醒");
    EXPECT(!fb_idle_poll(&idle, FB_IDLE_TIMEOUT_MS + 2000u),
           "唤醒后重新计时，不应立刻熄屏");
    EXPECT(!fb_idle_on_key(&idle, FB_IDLE_TIMEOUT_MS + 3000u),
           "亮屏下按键不该被吞");
    printf("  wake OK\n");
}

static void test_wrap(void) {
    fb_idle_t idle;
    /* 从回绕点前一秒起步，前进 100ms —— 跨过 0 但不能判成熄屏 */
    fb_idle_init(&idle, 0xFFFFFFFFu - 900u);
    EXPECT(!fb_idle_poll(&idle, 1000u), "回绕后 1.9 秒不该熄屏");
    EXPECT(fb_idle_poll(&idle, 0xFFFFFFFFu - 900u + FB_IDLE_TIMEOUT_MS + 1u),
           "回绕后仍能正常计到 60 秒");
    printf("  wrap OK\n");
}

int main(void) {
    printf("test_fb_idle\n");
    test_timeout();
    test_wake();
    test_wrap();
    if (s_fail) {
        printf("test_fb_idle: FAIL (%u)\n", s_fail);
        return 1;
    }
    printf("test_fb_idle: PASS\n");
    return 0;
}
