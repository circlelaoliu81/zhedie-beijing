#pragma once
/* 页面状态组装：把 L2 领域结果填入 xm_page_state_t，再交给纯 C VM。 */

#include "xm_huangli.h"
#include "xm_liuyao.h"
#include "xm_nav.h"
#include "xm_store.h"
#include "xm_time.h"
#include "xm_vm.h"

xm_view_t xm_pages_render(const xm_nav_t *nav,
                          const xm_time_t *time,
                          const xm_store_t *store,
                          const xm_liuyao_t *ly,
                          const uint8_t toss[6],
                          const xm_hl_table_t *hl_table,
                          int64_t now_us,
                          int soc,
                          uint8_t err_flags);
