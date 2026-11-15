#pragma once
/* 六爻铜钱状态机：6/7/8/9 → 本卦、动爻、之卦、静卦；随机源可注入。 */

#include "xm_types.h"

typedef xm_status_t (*xm_rng_fn)(void *ctx, uint8_t out[6]);

xm_liuyao_t xm_liuyao_from(const uint8_t toss[6]);
xm_status_t xm_liuyao_cast(xm_rng_fn rng, void *ctx, xm_liuyao_t *out);
uint8_t xm_liuyao_mark_flip(uint8_t moving_mask);
