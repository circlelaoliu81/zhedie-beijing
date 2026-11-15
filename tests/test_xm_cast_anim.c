/* test_xm_cast_anim.c —— 六爻「每按一次确定键摇一爻」的动画契约。
 *
 * 缺陷背景：旧版摇卦页只有一句静态「摇卦中…」，用户根本不知道按确定键
 * 会到下一爻；按 6 次确定键后直接跳结果页，中间没有任何可见状态。
 * 本测试守住：一次确定键只推进一爻、动画期间吃掉连按、tick 逐帧推进且
 * 铜钱位置真的变化、六爻齐后由用户明确按键才看卦象。
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_layout.h"
#include "xm_nav.h"
#include "xm_vm.h"

#define ANIM_US 720000LL

static void test_one_key_one_yao_and_animation(void) {
    xm_nav_t nav;
    xm_nav_result_t out;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_LY_CAST;
    nav.casting = true;

    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 0, &out) == XM_OK);
    assert(nav.cast_count == 1u);
    assert(nav.cast_phase == 1u);
    assert(out.cast_step);
    assert(nav.page == XM_PAGE_LY_CAST);
    assert(!out.finish_cast);

    /* 摇动中连按确定键必须被吃掉，不能一按到底跳过整卦。 */
    for (int i = 0; i < 5; ++i) {
        assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 1000, &out) == XM_OK);
        assert(nav.cast_count == 1u);
        assert(nav.page == XM_PAGE_LY_CAST);
    }

    bool render = false;
    assert(xm_nav_tick(&nav, 100000LL, &render) == XM_OK);
    assert(render);
    assert(nav.cast_frame == 1u);
    assert(nav.cast_phase == 1u);
    assert(xm_nav_tick(&nav, ANIM_US - 1, &render) == XM_OK);
    assert(nav.cast_phase == 1u);
    assert(xm_nav_tick(&nav, ANIM_US, &render) == XM_OK);
    assert(render);
    assert(nav.cast_phase == 0u);
    assert(nav.cast_frame == 0u);

    /* 落定后才能摇下一爻。 */
    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, ANIM_US, &out) == XM_OK);
    assert(nav.cast_count == 2u);
    assert(nav.cast_phase == 1u);
    printf("  一次确定键只摇一爻、动画期间连按被吃掉、tick 逐帧落定: PASS\n");
}

static void test_six_yao_then_explicit_key_to_result(void) {
    xm_nav_t nav;
    xm_nav_result_t out;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_LY_CAST;
    nav.casting = true;

    int64_t t = 0;
    for (uint8_t i = 0; i < 6u; ++i) {
        assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, t, &out) == XM_OK);
        assert(nav.cast_count == (uint8_t)(i + 1u));
        assert(nav.page == XM_PAGE_LY_CAST);
        assert(!out.finish_cast);
        bool render = false;
        assert(xm_nav_tick(&nav, t + ANIM_US, &render) == XM_OK);
        assert(render);
        assert(nav.cast_phase == 0u);
        t += ANIM_US;
    }
    /* 六爻齐了停在摇卦页，等用户明确按确定键才进结果页。 */
    assert(nav.page == XM_PAGE_LY_CAST);
    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, t, &out) == XM_OK);
    assert(nav.page == XM_PAGE_LY_RESULT);
    assert(out.finish_cast);
    printf("  六爻齐后停在本页、按确定键才看卦象: PASS\n");
}

static void test_long_press_abandons_and_clears_animation(void) {
    xm_nav_t nav;
    xm_nav_result_t out;
    xm_nav_init(&nav);
    nav.disclaimer_acked = true;
    nav.page = XM_PAGE_LY_CAST;
    nav.casting = true;
    assert(xm_nav_dispatch(&nav, XM_IN_OK_CLICK, 0, &out) == XM_OK);
    assert(nav.cast_phase == 1u);
    assert(xm_nav_dispatch(&nav, XM_IN_OK_LONG, 1000, &out) == XM_OK);
    assert(nav.cast_phase == 0u);
    assert(nav.cast_count == 0u);
    assert(nav.notice);
    printf("  长按确定键放弃本次并清掉动画状态: PASS\n");
}

/* 动画必须是「画面真的在变」，而不是只改了一个数字。
 * 逐帧渲染 LY_CAST，找出 radius=7 的铜钱外框，断言 x 坐标随帧变化。 */
static void test_animation_frames_change_pixels(void) {
    int16_t first_coin_x[8];
    for (uint8_t frame = 0u; frame < 8u; ++frame) {
        xm_page_state_t st;
        xm_vm_state_init(&st);
        st.page = XM_PAGE_LY_CAST;
        st.cast_count = 3u;
        st.cast_phase = 1u;
        st.cast_frame = frame;
        st.cast_toss[0] = 7u;
        st.cast_toss[1] = 7u;
        st.cast_toss[2] = 8u;
        const xm_view_t v = xm_vm_render(XM_PAGE_LY_CAST, &st);
        assert(xm_layout_assert_footer(&v));
        uint8_t coins = 0u;
        int16_t cx = -1;
        for (uint8_t i = 0; i < v.box_n; ++i) {
            if (v.boxes[i].radius == 7u) {
                if (coins == 0u) cx = v.boxes[i].x;
                ++coins;
            }
        }
        assert(coins == 3u);       /* 恰好三枚方孔铜钱 */
        first_coin_x[frame] = cx;
    }
    bool changed = false;
    for (uint8_t i = 1u; i < 8u; ++i) {
        if (first_coin_x[i] != first_coin_x[0]) changed = true;
    }
    assert(changed);
    printf("  8 帧渲染：三枚铜钱位置逐帧变化、全部过几何门禁: PASS\n");
}

static void test_settled_hides_coins(void) {
    xm_page_state_t st;
    xm_vm_state_init(&st);
    st.page = XM_PAGE_LY_CAST;
    st.cast_count = 3u;
    st.cast_phase = 0u;
    st.cast_toss[0] = 7u;
    st.cast_toss[1] = 8u;
    st.cast_toss[2] = 9u;
    const xm_view_t v = xm_vm_render(XM_PAGE_LY_CAST, &st);
    for (uint8_t i = 0; i < v.box_n; ++i) {
        assert(v.boxes[i].radius != 7u);   /* 落定后铜钱消失，露出爻线 */
    }
    assert(v.stroke_n >= 3u);
    assert(xm_layout_assert_footer(&v));
    printf("  落定后铜钱消失、爻线上屏: PASS\n");
}

int main(void) {
    printf("test_xm_cast_anim\n");
    test_one_key_one_yao_and_animation();
    test_six_yao_then_explicit_key_to_result();
    test_long_press_abandons_and_clears_animation();
    test_animation_frames_change_pixels();
    test_settled_hides_coins();
    printf("test_xm_cast_anim: PASS\n");
    return 0;
}
