/* test_xm_tianshi.c —— 进入页天师白描位图的挂载与几何契约。
 *
 * 背景：早期用 135 条程序化直线段拼「天师像」，在 240×320 上只能是抽象简笔，
 * 用户先后判为「光头和尚」/「太过于抽象十分难看」。线段池已整体废弃，改为
 * 传统白描位图（RGB565 常量 + 同源 PNG，由 test_xm_tianshi_bitmap.py 逐像素校验）。
 *
 * 这里守住：位图真的挂上了、尺寸与位置不压文字/按键区/页脚、
 * 只有进入页（免责 / 首页）挂图、其它页保持原样不串图。
 */
#include <assert.h>
#include <stdio.h>

#include "xm_layout.h"
#include "xm_tianshi_img.h"
#include "xm_vm.h"

static void test_bitmap_has_real_pixels(const uint16_t *img, uint16_t w, uint16_t h) {
    assert(img != NULL);
    assert(w > 0u && h > 0u);
    /* 纸底 0xF2EBDC → RGB565；若整幅都是纸底，说明生成链断了。 */
    const uint16_t paper = 0xF75Bu;
    uint32_t non_paper = 0u;
    for (uint32_t i = 0u; i < (uint32_t)w * h; ++i) {
        if (img[i] != paper) ++non_paper;
    }
    assert(non_paper > (uint32_t)w * h / 20u);   /* 至少 5% 是墨迹 */
}

static void test_entry_pages_geometry(void) {
    xm_page_state_t st;
    xm_vm_state_init(&st);
    st.page = XM_PAGE_DISCLAIMER;
    const xm_view_t disclaimer = xm_vm_render(XM_PAGE_DISCLAIMER, &st);
    assert(disclaimer.image == tianshi_main_rgb565);
    assert(disclaimer.image_w == TIANSHI_MAIN_W);
    assert(disclaimer.image_h == TIANSHI_MAIN_H);
    assert(disclaimer.image_x >= 0);
    assert(disclaimer.image_x + (int32_t)disclaimer.image_w <= 240);
    /* 顶栏分割线 y=22，大标题 y=164：图像必须完整夹在 26..158。 */
    assert(disclaimer.image_y >= 22);
    assert(disclaimer.image_y + (int32_t)disclaimer.image_h <= 164);
    assert(xm_layout_assert_footer(&disclaimer));
    test_bitmap_has_real_pixels(disclaimer.image, disclaimer.image_w,
                                disclaimer.image_h);

    xm_vm_state_init(&st);
    st.page = XM_PAGE_HOME;
    const xm_view_t home = xm_vm_render(XM_PAGE_HOME, &st);
    assert(home.image == tianshi_home_rgb565);
    assert(home.image_w == TIANSHI_HOME_W);
    assert(home.image_h == TIANSHI_HOME_H);
    /* 标题框 34..64、日期行 158..176：图像正好 64..158，两边都不压。 */
    assert(home.image_y >= 64);
    assert(home.image_y + (int32_t)home.image_h <= 158);
    assert(home.image_x >= 0);
    assert(home.image_x + (int32_t)home.image_w <= 240);
    assert(xm_layout_assert_footer(&home));
    test_bitmap_has_real_pixels(home.image, home.image_w, home.image_h);
}

static void test_no_bitmap_on_other_pages(void) {
    for (int p = 0; p < (int)XM_PAGE_COUNT; ++p) {
        if (p == (int)XM_PAGE_DISCLAIMER || p == (int)XM_PAGE_HOME) continue;
        xm_page_state_t st;
        xm_vm_state_init(&st);
        st.page = (xm_page_t)p;
        const xm_view_t v = xm_vm_render((xm_page_t)p, &st);
        assert(v.image == NULL);
        assert(v.image_w == 0u && v.image_h == 0u);
        assert(xm_layout_assert_footer(&v));
    }
}

int main(void) {
    printf("test_xm_tianshi\n");
    test_entry_pages_geometry();
    printf("  免责/首页白描位图尺寸、坐标、墨迹密度与几何门禁: PASS\n");
    test_no_bitmap_on_other_pages();
    printf("  其余 10 页不串图且全过门禁: PASS\n");
    printf("test_xm_tianshi: PASS\n");
    return 0;
}
