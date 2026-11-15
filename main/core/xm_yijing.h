#pragma once
/* XMYJ v1：64 卦 × 19 字段紧凑表索引，纯字符串池 + 定长条目。 */

#include "xm_types.h"

#define XM_YJ_FIELD_COUNT 19u
#define XM_YJ_GUA_COUNT 64u

typedef enum {
    XM_YJ_NAME = 0,
    XM_YJ_GUACI = 1,
    XM_YJ_XIANG = 2,
    XM_YJ_PLAIN = 3,
    XM_YJ_YAO_1 = 4,
    XM_YJ_YAO_2 = 5,
    XM_YJ_YAO_3 = 6,
    XM_YJ_YAO_4 = 7,
    XM_YJ_YAO_5 = 8,
    XM_YJ_YAO_6 = 9,
    XM_YJ_YI = 10,       /* 宜：此卦当下宜为之事（传统占断） */
    XM_YJ_JI = 11,       /* 忌：此卦当下忌为之事 */
    XM_YJ_TIMING = 12,   /* 时机：此卦所处的时势与走势 */
    /* 以下 6 字段为「解卦」核心：就卦象讲事业/财富/爱情/健康等生活维度。
     * 数据源 gua64_detail.tsv（傅佩荣式分维度解读），由 gen_yijing.py 抽取。 */
    XM_YJ_OVERALL = 13,  /* 总论：整卦综合解读 */
    XM_YJ_CAREER = 14,   /* 事业 */
    XM_YJ_WEALTH = 15,   /* 财富 */
    XM_YJ_LOVE = 16,     /* 爱情 */
    XM_YJ_HEALTH = 17,   /* 健康 */
    XM_YJ_HOME = 18      /* 家宅 */
} xm_yj_field_t;

typedef struct {
    uint8_t mark;       /* bit5=初爻，bit0=上爻；数值等于初爻为 MSB 的索引。 */
    uint16_t off[19];   /* 相对字符串池的字节偏移（19 字段）。 */
    uint8_t len[19];    /* UTF-8 字节长度，不含 NUL。 */
    uint8_t rsv;        /* 补齐 59B 条目（19×3 + 1 + 1），供后续扩展。 */
} XM_PACKED xm_gua_entry_t;

xm_status_t xm_yijing_init(void);
const char *xm_gua_name(uint8_t idx);
const char *xm_gua_text(uint8_t idx, uint8_t field);
uint8_t xm_gua_mark(uint8_t idx);
const char *xm_yao_label(uint8_t mark, uint8_t pos);
