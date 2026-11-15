/* test_xm_err_flags.c —— 异常态位标志的 host 测试。
 *
 * 重点保护 E1「时间未设定」的置位/清位规则：这套标志以前只置位不清位，
 * 导致用户设完时间后红条永久压在页面标题上（banner 画在 y=24..44）。 */

#include <assert.h>
#include <stdio.h>

#include "xm_err.h"
#include "xm_time.h"

static void test_e1_cleared_after_confirm(void) {
    xm_time_t time;
    xm_time_init(&time);
    /* 刚开机：本次尚未授时 → E1 必须置位。 */
    uint8_t flags = xm_err_flags_compute(time.confirmed, true, false, true);
    assert((flags & XM_ERR_TIME_UNCALIBRATED_BIT) != 0u);
    /* 用户完成授时（导航 OK 键 → xm_time_confirm）后必须清位。 */
    xm_date_t date = {2026, 11, 13, 10, 30};
    xm_time_confirm(&time, &date, 0);
    assert(time.confirmed);
    flags = xm_err_flags_compute(time.confirmed, true, false, true);
    assert((flags & XM_ERR_TIME_UNCALIBRATED_BIT) == 0u);
    assert(flags == 0u);
    printf("  E1 置位/清位: PASS\n");
}

static void test_other_flags(void) {
    /* E2 黄历过期：日期超区间时置位，且不影响 E1 的独立判定。 */
    uint8_t flags = xm_err_flags_compute(true, false, false, true);
    assert((flags & XM_ERR_HL_EXPIRED_BIT) != 0u);
    assert((flags & XM_ERR_TIME_UNCALIBRATED_BIT) == 0u);
    /* E3 存储降级。 */
    flags = xm_err_flags_compute(true, true, true, true);
    assert(flags == XM_ERR_NVS_DEGRADED_BIT);
    /* E5 内容缺失。 */
    flags = xm_err_flags_compute(true, true, false, false);
    assert(flags == XM_ERR_CONTENT_MISSING_BIT);
    /* 全部异常同时成立。 */
    flags = xm_err_flags_compute(false, false, true, false);
    assert(flags == (uint8_t)(XM_ERR_TIME_UNCALIBRATED_BIT | XM_ERR_HL_EXPIRED_BIT |
                              XM_ERR_NVS_DEGRADED_BIT | XM_ERR_CONTENT_MISSING_BIT));
    printf("  E2/E3/E5 独立置位: PASS\n");
}

static void test_time_confirm_sets_confirmed(void) {
    xm_time_t time;
    xm_time_init(&time);
    assert(!time.confirmed);
    xm_date_t date = {2027, 1, 1, 8, 0};
    xm_time_set(&time, &date, 1000);
    assert(time.confirmed);
    printf("  xm_time_set/confirm 置 confirmed: PASS\n");
}

int main(void) {
    printf("test_xm_err_flags\n");
    test_e1_cleared_after_confirm();
    test_other_flags();
    test_time_confirm_sets_confirmed();
    printf("test_xm_err_flags: PASS\n");
    return 0;
}
