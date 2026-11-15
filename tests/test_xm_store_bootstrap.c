/* test_xm_store_bootstrap.c —— 首次开机「缺键补齐」回归。
 *
 * 缺陷背景（真机烧录后发现，host 四轮复验全 PASS 也没覆盖）：
 *   xm_store_load 要求 fmt/ack/time/liuyao/settings 五个键齐全且各自可解码，
 *   但 xm_app_save_ack 只写 fmt+ack、save_time 只写 fmt+time、
 *   save_hist 只写 fmt+liuyao，settings 全工程从未写回。于是用户确认免责后
 *   的下一次开机必定判「存储降级」：E3 红条永久常驻、被钉回免责页、
 *   设过的时间不生效。全新设备（namespace 都还不存在）更是首开就报降级。
 *
 * 修复：补齐逻辑抽成纯函数 xm_store_load_boot（xm_store.c），键不存在时用
 * 默认状态编码的合法 blob 顶上；读到非空值仍原样严格校验。
 * 本测试直接盯住这个修复函数，并带三条负向对照。
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_store.h"

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAIL: %s\n", (msg));                                      \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static void make_default_blobs(uint8_t fmt[8], uint8_t ack[1],
                               uint8_t time_blob[12], uint8_t hist_blob[108],
                               uint8_t settings[8]) {
    xm_store_t s;
    xm_store_init(&s);
    assert(xm_store_encode_fmt(fmt) == XM_OK);
    assert(xm_store_encode_time(&s.time, time_blob) == XM_OK);
    assert(xm_store_save_hist(&s, hist_blob) == XM_OK);
    ack[0] = 0;
    memcpy(settings, s.settings, 8);
}

int main(void) {
    printf("test_xm_store_bootstrap\n");

    uint8_t fmt[8], ack[1], time_blob[12], hist_blob[108], settings[8];
    make_default_blobs(fmt, ack, time_blob, hist_blob, settings);

    /* 1. 完整五键：开机加载成功、不降级。 */
    xm_store_t loaded;
    xm_store_init(&loaded);
    CHECK(xm_store_load_boot(&loaded, fmt, 8, ack, 1, time_blob, 12,
                             hist_blob, 108, settings, 8) == XM_OK,
          "完整五键应能加载");
    CHECK(!loaded.degraded, "完整五键不应判降级");

    /* 2. 修复核心：save_ack 只留下 fmt+ack，缺三个键也必须正常加载。
     *    这是旧实现会失败、导致 E3 红条永久常驻的那条路径。 */
    xm_store_t partial;
    xm_store_init(&partial);
    CHECK(xm_store_load_boot(&partial, fmt, 8, ack, 1,
                             NULL, 0, NULL, 0, NULL, 0) == XM_OK,
          "只写 fmt+ack 时补齐应能加载");
    CHECK(!partial.degraded, "补齐后不应判降级");
    CHECK(partial.time.year == loaded.time.year &&
          partial.time.month == loaded.time.month &&
          partial.time.day == loaded.time.day,
          "补齐后日期应与默认状态一致");

    /* 3. 全新设备：五个键一个都没有，同样按默认状态启动。 */
    xm_store_t fresh;
    xm_store_init(&fresh);
    CHECK(xm_store_load_boot(&fresh, NULL, 0, NULL, 0, NULL, 0,
                             NULL, 0, NULL, 0) == XM_OK,
          "全空存档应能加载");
    CHECK(!fresh.degraded, "全新设备不应判降级");

    /* 4. 负向对照一：旧逻辑（xm_store_load 直传缺键）确实会失败，
     *    证明第 2 条不是「本来就没问题」。 */
    xm_store_t old_way;
    xm_store_init(&old_way);
    CHECK(xm_store_load(&old_way, fmt, 8, ack, 1,
                        NULL, 0, NULL, 0, NULL, 0) != XM_OK,
          "缺键直传 xm_store_load 应失败");
    CHECK(old_way.degraded, "直传缺键应标记降级");

    /* 5. 负向对照二：真坏数据不能被补齐掩盖，仍须判降级。 */
    uint8_t bad_time[12];
    memcpy(bad_time, time_blob, sizeof(bad_time));
    bad_time[0] ^= 0xFFu; /* 破坏年份 → CRC 校验必失败 */
    xm_store_t broken;
    xm_store_init(&broken);
    CHECK(xm_store_load_boot(&broken, fmt, 8, ack, 1, bad_time, 12,
                             hist_blob, 108, settings, 8) != XM_OK,
          "坏 time blob 应加载失败");
    CHECK(broken.degraded, "坏数据应标记降级");

    /* 6. 负向对照三：坏 fmt 也一样要判降级。 */
    uint8_t bad_fmt[8];
    memcpy(bad_fmt, fmt, sizeof(bad_fmt));
    bad_fmt[0] ^= 0xFFu;
    xm_store_t broken_fmt;
    xm_store_init(&broken_fmt);
    CHECK(xm_store_load_boot(&broken_fmt, bad_fmt, 8, ack, 1, time_blob, 12,
                             hist_blob, 108, settings, 8) != XM_OK,
          "坏 fmt blob 应加载失败");
    CHECK(broken_fmt.degraded, "坏 fmt 应标记降级");

    printf("  缺键/空存档可启动，三条负向对照均成立: PASS\n");
    printf("test_xm_store_bootstrap: PASS\n");
    return 0;
}
