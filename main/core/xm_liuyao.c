#include "xm_liuyao.h"

#include <string.h>

static bool xm_toss_valid(uint8_t v) {
    return v == 6u || v == 7u || v == 8u || v == 9u;
}

/*
 * D6 唯一位序转换点：moving_mask 的 bit0=初爻，而 ben_mark 的 bit5=初爻。
 * 这里显式把「按初→上的动爻位」映射到「初爻为 MSB 的卦码位」，其余模块禁止手写字位反转。
 */
uint8_t xm_liuyao_mark_flip(uint8_t moving_mask) {
    uint8_t flip = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        if ((moving_mask & (1u << i)) != 0) {
            flip |= (uint8_t)(1u << (5u - i));
        }
    }
    return (uint8_t)(flip & 0x3Fu);
}

xm_liuyao_t xm_liuyao_from(const uint8_t toss[6]) {
    xm_liuyao_t out;
    memset(&out, 0, sizeof(out));
    if (!toss) return out;
    uint8_t ben_mark = 0;
    uint8_t moving_mask = 0;
    uint8_t moving_cnt = 0;
    for (uint8_t i = 0; i < 6; ++i) {
        out.toss[i] = toss[i];
        if (!xm_toss_valid(toss[i])) {
            memset(&out, 0, sizeof(out));
            return out;
        }
        const uint8_t yang = (toss[i] == 7u || toss[i] == 9u) ? 1u : 0u;
        ben_mark = (uint8_t)((ben_mark << 1) | yang);
        if (toss[i] == 6u || toss[i] == 9u) {
            moving_mask |= (uint8_t)(1u << i);
            moving_cnt++;
        }
    }
    out.ben_mark = XM_GUA_IDX(ben_mark);
    out.ben_idx = XM_GUA_IDX(out.ben_mark);
    out.moving_mask = (uint8_t)(moving_mask & 0x3Fu);
    out.moving_cnt = moving_cnt;
    out.is_static = (moving_mask == 0);
    if (out.is_static) {
        out.changed_mark = out.ben_mark;
        out.changed_idx = out.ben_idx;
    } else {
        out.changed_mark = XM_GUA_IDX(out.ben_mark ^ xm_liuyao_mark_flip(out.moving_mask));
        out.changed_idx = XM_GUA_IDX(out.changed_mark);
    }
    return out;
}

xm_status_t xm_liuyao_cast(xm_rng_fn rng, void *ctx, xm_liuyao_t *out) {
    if (!rng || !out) return XM_ERR_INVALID_ARG;
    uint8_t toss[6] = {0};
    xm_status_t status = rng(ctx, toss);
    if (status != XM_OK) return status;
    *out = xm_liuyao_from(toss);
    for (uint8_t i = 0; i < 6; ++i) {
        if (!xm_toss_valid(out->toss[i])) return XM_ERR_INVALID_ARG;
    }
    return XM_OK;
}
