/* test_xm_yijing_jiegua.c —— 六十四卦「解卦」字段回归。
 *
 * 缺陷背景：用户反馈「六爻只有卦辞没有解卦」，并明确「真正的解卦需要就卦象讲
 * 对于事业/财富/爱情/健康等等维度的解读」——不是把卦辞/宜忌拆开显示。
 * 根因：gen_yijing.py 旧版只取 gua64.tsv 的卦辞/大象/白话等列，把生活维度解读
 * 整个丢弃。这些解读现成存在于 gua64_detail.tsv（傅佩荣式分维度解读，64/64 覆盖）。
 * 本轮补入 6 个分维度字段（总论/事业/财富/爱情/健康/家宅）。
 * 本测试锁死：64 卦的 6 个维度字段必须非空、非占位符。
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xm_yijing.h"
#include "xm_yijing_data.h"

static bool s_is_placeholder(const char *s) {
    return s == NULL || s[0] == '\0' || strcmp(s, "（整理本暂缺）") == 0;
}

int main(void) {
    printf("test_xm_yijing_jiegua\n");

    assert(xm_yijing_init() == XM_OK);
    assert(XM_YJ_FIELD_COUNT == 19u);

    static const xm_yj_field_t s_dims[] = {
        XM_YJ_OVERALL, XM_YJ_CAREER, XM_YJ_WEALTH,
        XM_YJ_LOVE, XM_YJ_HEALTH, XM_YJ_HOME
    };
    static const char *const s_dim_names[] = {
        "总论", "事业", "财富", "爱情", "健康", "家宅"
    };

    for (uint8_t idx = 0; idx < 64; ++idx) {
        const char *nm = xm_gua_name(idx);
        assert(!s_is_placeholder(nm));
        for (uint8_t d = 0; d < 6u; ++d) {
            const char *text = xm_gua_text(idx, (uint8_t)s_dims[d]);
            if (s_is_placeholder(text)) {
                printf("  缺失：%s 的%s\n", nm, s_dim_names[d]);
            }
            assert(!s_is_placeholder(text));
        }
    }

    /* 抽验几卦：乾的事业应为「…事业如日中天」；屯的财运含「买卖」；
     * 这些断言同时防止「字段错位」（把卦辞误填进事业等）。 */
    bool found_qian = false, found_zhun = false;
    for (uint8_t idx = 0; idx < 64; ++idx) {
        const char *nm = xm_gua_name(idx);
        if (nm && strcmp(nm, "乾") == 0) {
            found_qian = true;
            const char *career = xm_gua_text(idx, XM_YJ_CAREER);
            assert(career && strstr(career, "事业") != NULL);
        }
        if (nm && strcmp(nm, "屯") == 0) {
            found_zhun = true;
            const char *wealth = xm_gua_text(idx, XM_YJ_WEALTH);
            assert(wealth && strstr(wealth, "买卖") != NULL);
        }
    }
    assert(found_qian && found_zhun);

    printf("  64 卦总论/事业/财富/爱情/健康/家宅齐全: PASS\n");
    printf("test_xm_yijing_jiegua: PASS\n");
    return 0;
}
