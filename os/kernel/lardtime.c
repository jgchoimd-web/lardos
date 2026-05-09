#include "lardtime.h"
#include "rtc.h"

#include <stddef.h>

#define SECS_PER_DAY 86400LL

static int civil_is_leap(uint32_t y)
{
    return ((y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u));
}

static uint8_t civil_month_days(uint32_t y, uint8_t m)
{
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1u || m > 12u) return 0;
    if (m == 2u && civil_is_leap(y)) return 29;
    return mdays[m - 1u];
}

static int64_t days_before_year(uint32_t y)
{
    return (int64_t)y * 365LL + (int64_t)(y / 4u) - (int64_t)(y / 100u) + (int64_t)(y / 400u);
}

static int64_t days_before_month(uint32_t y, uint8_t m)
{
    int64_t d = 0;
    for (uint8_t i = 1; i < m; i++) d += civil_month_days(y, i);
    return d;
}

int64_t lardtime_from_civil(uint32_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute, uint8_t second)
{
    uint8_t dim = civil_month_days(year, month);
    int64_t days;
    if (dim == 0 || day < 1u || day > dim || hour > 23u || minute > 59u || second > 59u) {
        return -1;
    }
    days = days_before_year(year) + days_before_month(year, month) + (int64_t)(day - 1u);
    return days * SECS_PER_DAY + (int64_t)hour * 3600LL + (int64_t)minute * 60LL + (int64_t)second;
}

int lardtime_to_civil(int64_t ticks, lardtime_civil_t* out)
{
    int64_t days;
    int64_t rem;
    uint32_t y;
    uint16_t yday;
    uint8_t m;

    if (!out || ticks < 0) return -1;
    days = ticks / SECS_PER_DAY;
    rem = ticks % SECS_PER_DAY;
    y = (uint32_t)(days / 365LL);
    while (days_before_year(y + 1u) <= days) y++;
    while (days_before_year(y) > days) y--;
    yday = (uint16_t)(days - days_before_year(y));

    m = 1;
    while (m < 12u) {
        uint8_t dim = civil_month_days(y, m);
        if (yday < dim) break;
        yday = (uint16_t)(yday - dim);
        m++;
    }

    out->year = y;
    out->month = m;
    out->day = (uint8_t)(yday + 1u);
    out->hour = (uint8_t)(rem / 3600LL);
    out->minute = (uint8_t)((rem / 60LL) % 60LL);
    out->second = (uint8_t)(rem % 60LL);
    out->yday = (uint16_t)(days - days_before_year(y));
    out->wday = (uint8_t)((days + 6LL) % 7LL);
    return 0;
}

int64_t lardtime_unix_to_ticks(int64_t unix_seconds)
{
    int64_t unix_epoch = lardtime_from_civil(1970u, 1u, 1u, 0u, 0u, 0u);
    if (unix_epoch < 0) return 0;
    return unix_seconds + unix_epoch;
}

static int lunar_year_has_leap6(uint32_t y)
{
    uint32_t c = y % 19u;
    return c == 0u || c == 3u || c == 6u || c == 8u ||
           c == 11u || c == 14u || c == 17u;
}

static uint8_t lunar_month_len(uint32_t y, uint8_t m, uint8_t leap)
{
    uint32_t mix = y + (uint32_t)m + (leap ? 5u : 0u);
    return (mix & 1u) ? 29u : 30u;
}

static void lunar_next(uint32_t* y, uint8_t* m, uint8_t* leap)
{
    if (*leap) {
        *leap = 0;
        (*m)++;
    } else if (*m == 6u && lunar_year_has_leap6(*y)) {
        *leap = 1;
    } else {
        (*m)++;
    }
    if (*m > 12u) {
        *m = 1;
        *leap = 0;
        (*y)++;
    }
}

static void lunar_prev(uint32_t* y, uint8_t* m, uint8_t* leap)
{
    if (*leap) {
        *leap = 0;
        return;
    }
    if (*m == 1u) {
        if (*y > 0u) (*y)--;
        *m = 12u;
        *leap = 0;
        return;
    }
    if (*m == 7u && lunar_year_has_leap6(*y)) {
        *m = 6u;
        *leap = 1;
        return;
    }
    (*m)--;
    *leap = 0;
}

void lardtime_lunar_from_civil(const lardtime_civil_t* civil, lardtime_lunar_t* out)
{
    int64_t anchor = lardtime_from_civil(2026u, 2u, 17u, 0u, 0u, 0u) / SECS_PER_DAY;
    int64_t target;
    int64_t delta;
    uint32_t y = 2026u;
    uint8_t m = 1u;
    uint8_t leap = 0u;

    if (!civil || !out) return;
    target = lardtime_from_civil(civil->year, civil->month, civil->day, 0u, 0u, 0u) / SECS_PER_DAY;
    delta = target - anchor;

    while (delta >= lunar_month_len(y, m, leap)) {
        delta -= lunar_month_len(y, m, leap);
        lunar_next(&y, &m, &leap);
    }
    while (delta < 0) {
        lunar_prev(&y, &m, &leap);
        delta += lunar_month_len(y, m, leap);
    }

    out->year = y;
    out->month = m;
    out->day = (uint8_t)(delta + 1);
    out->leap_month = leap;
}

uint32_t lardtime_dangun_year(uint32_t civil_year)
{
    return civil_year + LARDTIME_DANGUN_OFFSET;
}

int64_t lardtime_now_ticks(void)
{
    int64_t unix_seconds = rtc_unix_seconds();
    if (unix_seconds <= 0) return 0;
    return lardtime_unix_to_ticks(unix_seconds);
}

int lardtime_now(lardtime_snapshot_t* out)
{
    int64_t ticks;
    if (!out) return -1;
    ticks = lardtime_now_ticks();
    if (ticks <= 0 || lardtime_to_civil(ticks, &out->civil) != 0) return -2;
    out->ticks = ticks;
    out->dangun_year = lardtime_dangun_year(out->civil.year);
    lardtime_lunar_from_civil(&out->civil, &out->lunar);
    return 0;
}

int lardtime_selftest(void)
{
    lardtime_civil_t c;
    lardtime_lunar_t l;
    int64_t t1970 = lardtime_from_civil(1970u, 1u, 1u, 0u, 0u, 0u);
    int64_t t10000 = lardtime_from_civil(10000u, 1u, 1u, 0u, 0u, 0u);
    int64_t anchor = lardtime_from_civil(2026u, 2u, 17u, 0u, 0u, 0u);
    if (t1970 <= 0 || t10000 <= t1970 || anchor <= 0) return -1;
    if (lardtime_to_civil(t10000, &c) != 0 || c.year != 10000u || c.month != 1u || c.day != 1u) return -2;
    if (lardtime_to_civil(anchor, &c) != 0) return -3;
    lardtime_lunar_from_civil(&c, &l);
    if (l.year != 2026u || l.month != 1u || l.day != 1u || l.leap_month) return -4;
    if (lardtime_dangun_year(2026u) != 4359u) return -5;
    if (lardtime_unix_to_ticks(0) != t1970) return -6;
    return 0;
}
