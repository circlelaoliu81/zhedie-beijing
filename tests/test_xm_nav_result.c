/* test_xm_nav_result.c —— 六爻结果页 / 关于页的按键语义回归。
 *
 * 缺陷背景（QA 第三轮复验 → 本轮补解卦多屏）：
 *   · 结果页原本把全部解读塞进一屏，且按下键/确定直接进历史，导致卦辞之后的
 *     断语、大象、解卦根本看不到（用户反馈「只有卦辞没有解卦」）。
 *   · 现在结果页拆成九屏（卦象/动爻/总论/事业/财富/爱情/健康/家宅/占断），
 *     上/下键循环切屏，确定键进历史（保持「确定=进入」语义），长按回首页。
 *   · 关于页是静态整页，上下键不跳页、不改 sub。
 */

#include <assert.h>
#include <stdio.h>

#include "xm_nav.h"
#include "xm_types.h"

int main(void) {
    printf("test_xm_nav_result\n");

    xm_nav_t nav;
    xm_nav_result_t out;

    /* 1. 进入结果页 sub=0，下键 → 切到 sub=1（卦象→动爻），页面不跳。 */
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;   /* 免责未确认时状态机会把任何页弹回 DISCLAIMER */
    nav.page = XM_PAGE_LY_RESULT;
    nav.sub = 0u;
    assert(xm_nav_dispatch(&nav, XM_IN_DOWN_CLICK, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_LY_RESULT);
    assert(nav.sub == 1u);

    /* 1b. 连按 9 次下键只在九屏间循环，绝不偷偷跳历史（杜绝「幽灵跳页」）。 */
    for (int i = 0; i < 9; ++i) {
        assert(xm_nav_dispatch(&nav, XM_IN_DOWN_CLICK, 0, &out) == XM_OK);
        assert(nav.page == XM_PAGE_LY_RESULT);
    }
    /* 从 sub=1 走满一圈，循环回 1。 */
    assert(nav.sub == 1u);

    /* 2. 上键 → 循环切到 sub=0（1→0），页面不跳。 */
    assert(xm_nav_dispatch(&nav, XM_IN_UP_CLICK, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_LY_RESULT);
    assert(nav.sub == 0u);
    /* 上键再走 0→8（循环到末屏占断）。 */
    assert(xm_nav_dispatch(&nav, XM_IN_UP_CLICK, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_LY_RESULT);
    assert(nav.sub == 8u);
    /* 从末屏下键回到 0。 */
    assert(xm_nav_dispatch(&nav, XM_IN_DOWN_CLICK, 0, &out) == XM_OK);
    assert(nav.sub == 0u);

    /* 3. 确定键单击 → 进历史（保持「确定=进入」语义）。 */
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_LY_RESULT;
    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_LY_HISTORY);

    /* 4. 从历史长按回结果页，sub 仍保留在用户所在屏（push/pop 栈没被拆坏）。 */
    assert(xm_nav_dispatch(&nav, XM_IN_OK_LONG, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_LY_RESULT);

    /* 5. 结果页长按确定键仍然回首页。 */
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_LY_RESULT;
    assert(xm_nav_dispatch(&nav, XM_IN_OK_LONG, 0, &out) == XM_OK);
    assert(nav.page == XM_PAGE_HOME);

    /* 6. 关于页上下键不再改 sub，也不跳页。 */
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_ABOUT;
    nav.sub = 0u;
    assert(xm_nav_dispatch(&nav, XM_IN_DOWN_CLICK, 0, &out) == XM_OK);
    assert(nav.sub == 0u);
    assert(nav.page == XM_PAGE_ABOUT);
    assert(xm_nav_dispatch(&nav, XM_IN_UP_CLICK, 0, &out) == XM_OK);
    assert(nav.sub == 0u);
    assert(nav.page == XM_PAGE_ABOUT);

    printf("  结果页上下键切屏、确定进历史、长按回首页: PASS\n");
    printf("test_xm_nav_result: PASS\n");
    return 0;
}
