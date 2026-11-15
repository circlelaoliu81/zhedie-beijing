/* 自动生成：请勿手改。XMHL v1 黄历查表声明。 */
#pragma once
#include "xm_types.h"

/* 黄历数据 blob（const，置于 Flash .rodata，不占内部 SRAM）。 */
extern const uint8_t xm_huangli_blob[];
extern const uint32_t xm_huangli_blob_size;

/* 固定查表：建除十二神 / 二十八宿 / 八方位 / 三十纳音 / 六十甲子。 */
extern const char *const xm_zhixing_table[12];
extern const char *const xm_xiu_table[28];
extern const char *const xm_dir_table[8];
extern const char *const xm_nayin_table[30];
extern const char *const xm_jiazi_table[60];
