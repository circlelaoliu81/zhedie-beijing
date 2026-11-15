/* 折叠北京 · 存档契约测试（host）
 *
 * 存档是唯一会造成「玩家进度无声消失」的模块，所以守三件事：
 *   1. 往返一致：pack → unpack 后每个影响玩法的字段都必须一模一样
 *   2. 坏档必拒：缺字节 / magic 错 / 版本错 / 内容被改，一个都不能读回来
 *   3. 落地位置：读档后停在封面（不是主面板），并且知道有档可续
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fb_save.h"

static uint32_t s_fail;

#define EXPECT(cond, msg)                                    \
    do {                                                     \
        if (!(cond)) {                                       \
            printf("  FAIL %s (line %d)\n", (msg), __LINE__); \
            ++s_fail;                                        \
        }                                                    \
    } while (0)

static void test_roundtrip(void) {
    fb_app_t a;
    fb_app_init(&a, 12345u);
    /* 造一个「玩了几十天」的状态 */
    a.st.day = 47;
    a.st.cash = 3820;
    a.st.debt = 1200;
    a.st.savings = 5000;
    a.st.health = 63;
    a.st.stamina = 41;
    a.st.reputation = 58;
    a.st.storage = 24;
    a.st.fans = 1900;
    a.st.has_ebike = 1;
    a.st.has_gear = 1;
    a.st.has_car = 0;
    a.st.has_live = 1;
    a.st.hours_used = 11;
    a.st.inv[0] = 3;
    a.st.inv[2] = 2;
    a.st.inv_total = 5;
    a.st.sleep_debt = 3;
    a.st.drowsy = FB_DROWSY_TIRED;
    a.st.faint_cnt = 1;
    a.st.live_streak = 6;
    a.st.news_idx = 4;
    a.st.spot = 3;
    a.st.day_earned = 320;
    a.st.day_spent = 180;
    a.line_sel = 4;
    a.home_sel = 2;
    a.bg_sel = 1;

    uint8_t buf[FB_SAVE_MAX];
    size_t n = fb_save_pack(&a, buf, sizeof(buf));
    EXPECT(n > 0, "pack 应成功");
    EXPECT(n <= FB_SAVE_MAX, "存档不得超过 FB_SAVE_MAX");

    /* 同一次状态连打两次，字节必须完全一致（crc 不能受填充字节影响） */
    uint8_t buf2[FB_SAVE_MAX];
    size_t n2 = fb_save_pack(&a, buf2, sizeof(buf2));
    EXPECT(n == n2 && memcmp(buf, buf2, n) == 0, "pack 必须确定性");

    fb_app_t b;
    fb_app_init(&b, 999u);   /* 用一个不同的种子，确保下面看到的是存档里的 */
    EXPECT(fb_save_unpack(&b, buf, n), "unpack 应成功");

    EXPECT(b.st.day == 47, "day");
    EXPECT(b.st.cash == 3820, "cash");
    EXPECT(b.st.debt == 1200, "debt");
    EXPECT(b.st.savings == 5000, "savings");
    EXPECT(b.st.health == 63, "health");
    EXPECT(b.st.stamina == 41, "stamina");
    EXPECT(b.st.reputation == 58, "reputation");
    EXPECT(b.st.storage == 24, "storage");
    EXPECT(b.st.fans == 1900, "fans");
    EXPECT(b.st.has_ebike == 1 && b.st.has_gear == 1, "装备");
    EXPECT(b.st.has_live == 1, "直播设备");
    EXPECT(b.st.hours_used == 11, "hours_used");
    EXPECT(b.st.inv[0] == 3 && b.st.inv[2] == 2 && b.st.inv_total == 5, "库存");
    EXPECT(b.st.sleep_debt == 3, "sleep_debt");
    EXPECT(b.st.drowsy == FB_DROWSY_TIRED, "drowsy");
    EXPECT(b.st.live_streak == 6, "live_streak");
    EXPECT(b.st.news_idx == 4, "news_idx");
    EXPECT(b.st.spot == 3, "spot");
    EXPECT(b.st.day_earned == 320, "day_earned");
    EXPECT(b.st.day_spent == 180, "day_spent");
    EXPECT(b.line_sel == 4 && b.home_sel == 2 && b.bg_sel == 1, "UI 光标");
    EXPECT(b.seed == 12345u, "seed");

    /* 需求矩阵要原样带回来，否则读档后摆摊收益会莫名其妙变 */
    EXPECT(memcmp(a.st.demand, b.st.demand, sizeof(a.st.demand)) == 0,
           "需求矩阵");

    /* 读档后停在封面，等玩家自己点「继续」 */
    EXPECT(b.scr == FB_SCR_INTRO, "读档后应停在封面");
    EXPECT(b.have_save, "have_save 应为 true");
    EXPECT(b.save_day == 47, "save_day");
    EXPECT(b.sel == 0, "封面光标应在「继续」");

    printf("  roundtrip OK (%u bytes)\n", (unsigned)n);
}

static void test_bad_save(void) {
    fb_app_t a;
    fb_app_init(&a, 777u);
    a.st.day = 12;
    uint8_t buf[FB_SAVE_MAX];
    size_t n = fb_save_pack(&a, buf, sizeof(buf));
    EXPECT(n > 0, "基准存档应能打出来");

    fb_app_t b;

    /* 长度不足 */
    fb_app_init(&b, 1u);
    EXPECT(!fb_save_unpack(&b, buf, 4), "短到不够头部的数据必须拒");
    EXPECT(b.scr == FB_SCR_INTRO && b.st.day == 1, "拒绝时不得改动 app");

    /* 每一位被翻转都必须被 crc 抓住 */
    uint8_t tmp[FB_SAVE_MAX];
    for (size_t i = 0; i < n; ++i) {
        memcpy(tmp, buf, n);
        tmp[i] = (uint8_t)(tmp[i] ^ 0xA5);
        EXPECT(!fb_save_unpack(&b, tmp, n), "被篡改的存档必须拒");
    }

    /* 已结束的局不该存 */
    fb_app_t c;
    fb_app_init(&c, 5u);
    c.st.ending = FB_END_DEAD;
    EXPECT(fb_save_pack(&c, buf, sizeof(buf)) == 0, "结束的局不该存档");

    printf("  bad save OK（逐字节篡改 %u 次全拒）\n", (unsigned)n);
}

static void test_clear_after_end(void) {
    /* 局结束后重开：状态机应给出 save_clear，让设备层删档 */
    fb_app_t a;
    fb_app_init(&a, 4242u);
    fb_app_on_key(&a, XM_IN_OK_CLICK);      /* 封面 → 主面板 */
    assert(a.scr == FB_SCR_HOME);

    a.st.ending = FB_END_SETTLE;
    a.st.day = FB_TOTAL_DAYS;
    a.scr = FB_SCR_ENDING;
    fb_app_on_key(&a, XM_IN_OK_CLICK);      /* 结局页 → 重开 */
    EXPECT(a.scr == FB_SCR_INTRO, "结局后回封面");
    EXPECT(a.save_clear, "结局后应要求删档");
    EXPECT(a.st.ending == FB_END_NONE, "新局不应带着旧结局");

    printf("  clear-after-end OK\n");
}

int main(void) {
    printf("test_fb_save\n");
    test_roundtrip();
    test_bad_save();
    test_clear_after_end();
    if (s_fail) {
        printf("test_fb_save: FAIL (%u)\n", s_fail);
        return 1;
    }
    printf("test_fb_save: PASS\n");
    return 0;
}
