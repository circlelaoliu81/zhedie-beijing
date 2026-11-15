#pragma once
/* 玄门跨层公共类型：纯 ISO C11，不含 ESP-IDF / LVGL / FreeRTOS 依赖。 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int xm_status_t;

/* 紧凑结构体属性：保证 xm_gua_entry_t 等定长数据表严格 32B，不被编译器插入对齐填充。 */
#if defined(__GNUC__) || defined(__clang__)
#define XM_PACKED __attribute__((packed))
#else
#define XM_PACKED
#endif

enum {
    XM_OK = 0,
    XM_ERR_BAD_DATE = -1,
    XM_ERR_HL_OUT_OF_RANGE = -2,
    XM_ERR_HL_BAD_BLOB = -3,
    XM_ERR_HL_FIELD_MISSING = -4,
    XM_ERR_IDX_OUT_OF_RANGE = -5,
    XM_ERR_TEXTS_BAD = -6,
    XM_ERR_NVS_BAD_FORMAT = -7,
    XM_ERR_NVS_IO = -8,
    XM_ERR_TIME_UNCALIBRATED = -9,
    XM_ERR_BATTERY_UNAVAILABLE = -10,
    XM_ERR_NO_MEM = -11,
    XM_ERR_INVALID_ARG = -12
};

/* 异常态位标志：统一用朱砂提示条叠加，不占页脚免责区域。 */
#define XM_ERR_TIME_UNCALIBRATED_BIT 0x01u
#define XM_ERR_HL_EXPIRED_BIT       0x02u
#define XM_ERR_NVS_DEGRADED_BIT     0x04u
#define XM_ERR_BATTERY_UNAVAILABLE_BIT 0x08u
#define XM_ERR_CONTENT_MISSING_BIT  0x10u

/* 数据表与 NVS 的 magic / version 约定，全部小端、version=1。 */
#define XM_MAGIC_HL "XMHL"
#define XM_MAGIC_YJ "XMYJ"
#define XM_MAGIC_NVS "XMN1"
#define XM_HIST_MAGIC_0 'X'
#define XM_HIST_MAGIC_1 'H'
#define XM_FORMAT_VERSION 1u

/* 黄历覆盖 2026-01-01 ~ 2028-12-31，共 1096 天。 */
#define XM_HL_DAY_COUNT 1096u
#define XM_HL_BASE_DAYS 20454
#define XM_DATA_START_Y 2026
#define XM_DATA_END_Y 2028
#define XM_DATA_END_M 12
#define XM_DATA_END_D 31

/*
 * D6 卦码：mark[0] = 初爻（最下），mark[5] = 上爻；初爻是最高位 bit5。
 * 索引 = 爻1*32 + 爻2*16 + 爻3*8 + 爻4*4 + 爻5*2 + 爻6*1。
 * 全工程只用 XM_GUA_IDX / XM_GUA_LINE / XM_GUA_MARK_FROM_LINES，禁止手写位反转。
 */
#define XM_GUA_IDX(m) ((uint8_t)((m) & 0x3Fu))
#define XM_GUA_LINE(m, pos) ((uint8_t)(((m) >> (6u - (uint8_t)(pos))) & 1u))
#define XM_GUA_MARK_FROM_LINES(l) ((uint8_t)( \
    (((l)[0] & 1u) << 5) | (((l)[1] & 1u) << 4) | (((l)[2] & 1u) << 3) | \
    (((l)[3] & 1u) << 2) | (((l)[4] & 1u) << 1) | (((l)[5] & 1u) << 0)))

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
} xm_date_t;

typedef struct {
    xm_date_t date;
    uint8_t second; /* 本设备只承诺分钟级，恒为 0。 */
} xm_datetime_t;

typedef struct {
    const char *items[32];
    uint8_t count;
} xm_field_t;

typedef enum {
    XM_PAGE_DISCLAIMER = 0,  /* P-00 */
    XM_PAGE_KEYGUIDE,        /* P-00b 三键操作引导（首启一次，免责确认后、授时前） */
    XM_PAGE_TIME,            /* P-01 */
    XM_PAGE_HOME,            /* P-02 */
    XM_PAGE_HL_A, XM_PAGE_HL_B, XM_PAGE_HL_C,
    XM_PAGE_LY_CAST,         /* P-04 */
    XM_PAGE_LY_RESULT,       /* P-05 */
    XM_PAGE_LY_HISTORY,      /* P-06 */
    /* P-07 / P-07b / P-08 三个页面已随功能移除：设备无摄像头、无罗盘，取不到真实输入，
     * 不做假装能看的页面。工程里没有任何持久化页号（xm_store_t / xm_hist_rec_t 都不存
     * page 字段），因此这里是压缩编号、也没留空位；以后再加页面直接往后排即可。 */
    XM_PAGE_SETTINGS,        /* P-09 */
    XM_PAGE_ABOUT,           /* P-10 */
    XM_PAGE_COUNT
} xm_page_t;

typedef enum {
    XM_IN_NONE = 0,
    XM_IN_UP_CLICK,
    XM_IN_DOWN_CLICK,
    XM_IN_OK_CLICK,
    XM_IN_OK_LONG,
    /* 仅为兼容旧枚举取值保留：输入层已按官方语义把连按归一成一次 CLICK，
     * 状态机不再产生也不再消费任何 *_DOUBLE 事件。 */
    XM_IN_OK_DOUBLE,
    XM_IN_UP_DOUBLE,
    XM_IN_DOWN_DOUBLE
} xm_input_t;

typedef struct {
    uint32_t dev_time;
    uint16_t toss_pack;
    uint8_t ben_idx;
    uint8_t changed_idx;
    uint8_t moving_mask;
    uint8_t rsv[3];
} xm_hist_rec_t;

typedef struct {
    xm_page_t page;
    xm_page_t stack[8];
    uint8_t depth;
    bool disclaimer_acked;
    bool time_confirmed;
    bool casting;
    bool review_mode;
    bool notice;
    bool confirm_clear;
    uint8_t cast_count;
    /* 六爻摇卦动画：0=等待确定键，1=本爻正在摇动。
     * 输入任务 100ms tick 推进 cast_frame，每按一次确定键只推进一爻；
     * 摇动期间吃掉确定键，避免连按直接跳过整卦。 */
    uint8_t cast_phase;
    uint8_t cast_frame;
    int64_t cast_anim_us;
    uint8_t sel;
    uint8_t sub;
    uint8_t time_cursor;
    uint8_t err_flags;
    xm_date_t draft_date;
    int64_t notice_until_us;
    xm_page_t pending_page;
} xm_nav_t;

typedef struct {
    xm_date_t base;
    int64_t base_us;
    bool confirmed;
    bool calibrated;
} xm_time_t;

typedef struct {
    uint8_t toss[6];
    uint8_t ben_mark;
    uint8_t ben_idx;
    uint8_t changed_mark;
    uint8_t changed_idx;
    uint8_t moving_mask;
    uint8_t moving_cnt;
    bool is_static;
} xm_liuyao_t;
