#pragma once
/* 纯 C11 时间模型：手动基准 + 单调时钟推进，供固件与 host tests 共用。 */

#include "xm_types.h"

int64_t xm_days_from_civil(int year, int month, int day);
void xm_civil_from_days(int64_t days, int *year, int *month, int *day);
uint8_t xm_time_days_in_month(uint16_t year, uint8_t month);
bool xm_date_valid(const xm_date_t *date);
void xm_time_init(xm_time_t *time);
void xm_time_set(xm_time_t *time, const xm_date_t *date, int64_t now_us);
void xm_time_confirm(xm_time_t *time, const xm_date_t *date, int64_t now_us);
xm_datetime_t xm_time_now(const xm_time_t *time, int64_t now_us);
bool xm_time_in_range(const xm_date_t *date);
xm_status_t xm_time_format(const xm_datetime_t *datetime,
                           char *date_out, size_t date_cap,
                           char *time_out, size_t time_cap);
