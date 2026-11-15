#include "xm_time.h"

#include <stdio.h>
#include <string.h>

/* Howard Hinnant 公历→连续日序：纯整数，1970-01-01 返回 0。 */
int64_t xm_days_from_civil(int year, int month, int day) {
    int64_t y = (int64_t)year;
    y -= month <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t mp = (int64_t)month + (month > 2 ? -3 : 9);
    const int64_t doy = (153 * mp + 2) / 5 + (int64_t)day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* days_from_civil 的逆运算，用于基准时间跨日/月/年推进。 */
void xm_civil_from_days(int64_t days, int *year, int *month, int *day) {
    int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    if (year) *year = (int)y;
    if (month) *month = (int)m;
    if (day) *day = (int)d;
}

uint8_t xm_time_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t s_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return s_days[month - 1];
}

bool xm_date_valid(const xm_date_t *date) {
    if (!date) return false;
    if (date->year < 1970 || date->year > 9999) return false;
    if (date->hour > 23 || date->minute > 59) return false;
    const uint8_t dim = xm_time_days_in_month(date->year, date->month);
    return dim != 0 && date->day >= 1 && date->day <= dim;
}

void xm_time_init(xm_time_t *time) {
    if (!time) return;
    memset(time, 0, sizeof(*time));
    time->base.year = 2026;
    time->base.month = 11;
    time->base.day = 13;
    time->base.hour = 10;
    time->base.minute = 30;
    time->base_us = 0;
    time->confirmed = false;
    time->calibrated = false;
}

void xm_time_set(xm_time_t *time, const xm_date_t *date, int64_t now_us) {
    if (!time || !date) return;
    time->base = *date;
    time->base_us = now_us;
    time->confirmed = true;
    time->calibrated = false;
}

void xm_time_confirm(xm_time_t *time, const xm_date_t *date, int64_t now_us) {
    xm_time_set(time, date, now_us);
}

xm_datetime_t xm_time_now(const xm_time_t *time, int64_t now_us) {
    xm_datetime_t out;
    memset(&out, 0, sizeof(out));
    if (!time) return out;
    int64_t elapsed_us = now_us - time->base_us;
    if (elapsed_us < 0) elapsed_us = 0;
    const int64_t elapsed_minutes = elapsed_us / 60000000LL;
    const int64_t base_days = xm_days_from_civil(time->base.year,
                                                 time->base.month,
                                                 time->base.day);
    const int64_t base_minutes = (int64_t)time->base.hour * 60 + time->base.minute;
    const int64_t total_minutes = base_minutes + elapsed_minutes;
    const int64_t day_delta = total_minutes / 1440;
    const int64_t minute_of_day = total_minutes % 1440;
    int y = 0;
    int m = 0;
    int d = 0;
    xm_civil_from_days(base_days + day_delta, &y, &m, &d);
    out.date.year = (uint16_t)y;
    out.date.month = (uint8_t)m;
    out.date.day = (uint8_t)d;
    out.date.hour = (uint8_t)(minute_of_day / 60);
    out.date.minute = (uint8_t)(minute_of_day % 60);
    out.second = 0;
    return out;
}

bool xm_time_in_range(const xm_date_t *date) {
    if (!xm_date_valid(date)) return false;
    if (date->year < XM_DATA_START_Y || date->year > XM_DATA_END_Y) return false;
    const int64_t days = xm_days_from_civil(date->year, date->month, date->day);
    const int64_t start = xm_days_from_civil(XM_DATA_START_Y, 1, 1);
    const int64_t end = xm_days_from_civil(XM_DATA_END_Y, 12, 31);
    return days >= start && days <= end;
}

xm_status_t xm_time_format(const xm_datetime_t *datetime,
                           char *date_out, size_t date_cap,
                           char *time_out, size_t time_cap) {
    if (!datetime || !date_out || !time_out || date_cap < 11 || time_cap < 6) {
        return XM_ERR_INVALID_ARG;
    }
    (void)snprintf(date_out, date_cap, "%04u-%02u-%02u",
                   (unsigned)datetime->date.year,
                   (unsigned)datetime->date.month,
                   (unsigned)datetime->date.day);
    (void)snprintf(time_out, time_cap, "%02u:%02u",
                   (unsigned)datetime->date.hour,
                   (unsigned)datetime->date.minute);
    return XM_OK;
}
