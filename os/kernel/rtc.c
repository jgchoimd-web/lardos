#include "rtc.h"
#include "io.h"

#include <stddef.h>
#include <stdint.h>

static void io_wait(void)
{
    outb(0x80, 0);
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, (uint8_t)(reg | 0x80u));
    io_wait();
    return inb(0x71);
}

static int bcd_or_bin(uint8_t v, int bcd)
{
    if (bcd) return (int)((v >> 4) * 10 + (v & 0x0F));
    return (int)v;
}

static int is_leap(int y)
{
    return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

static int64_t civil_to_unix(int y, int mo, int d, int hh, int mm, int ss)
{
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int64_t days = 0;
    int yy;
    for (yy = 1970; yy < y; yy++) {
        days += 365;
        if (is_leap(yy)) days++;
    }
    for (int i = 1; i < mo; i++) {
        int dim = mdays[i - 1];
        if (i == 2 && is_leap(y)) dim = 29;
        days += dim;
    }
    days += (int64_t)(d - 1);
    return days * 86400LL + (int64_t)hh * 3600 + (int64_t)mm * 60 + (int64_t)ss;
}

static void unix_to_civil(int64_t t, struct tm* tm)
{
    if (!tm) return;
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    tm->tm_sec = (int)(rem % 60);
    tm->tm_min = (int)((rem / 60) % 60);
    tm->tm_hour = (int)(rem / 3600);

    int y = 1970;
    for (;;) {
        int diy = is_leap(y) ? 366 : 365;
        if (days < diy) break;
        days -= diy;
        y++;
    }
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = 1;
    for (;;) {
        int dim = mdays[m - 1];
        if (m == 2 && is_leap(y)) dim = 29;
        if (days < dim) break;
        days -= dim;
        m++;
    }
    tm->tm_year = y - 1900;
    tm->tm_mon = m - 1;
    tm->tm_mday = (int)days + 1;
    tm->tm_wday = 0;
    tm->tm_yday = 0;
    tm->tm_isdst = 0;
}

int64_t rtc_unix_seconds(void)
{
    uint8_t sb = cmos_read(0x0B);
    int bcd = (sb & 0x04u) == 0;

    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);

    int s = bcd_or_bin(sec, bcd);
    int mi = bcd_or_bin(min, bcd);
    int h = bcd_or_bin(hour, bcd & ((sb & 0x02u) == 0));
    if (sb & 0x02u) {
        /* 12-hour mode not supported in this minimal path */
        if (h < 0 || h > 23) return 0;
    }
    int d = bcd_or_bin(day, bcd);
    int mo = bcd_or_bin(month, bcd);
    int y2 = bcd_or_bin(year, bcd);
    int full_year = (y2 < 70) ? (2000 + y2) : (1900 + y2);

    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return civil_to_unix(full_year, mo, d, h, mi, s);
}

time_t rtc_time(time_t* tp)
{
    time_t t = (time_t)rtc_unix_seconds();
    if (tp) *tp = t;
    return t;
}

struct tm* rtc_gmtime_r(const time_t* tt, struct tm* tm_buf)
{
    if (!tt || !tm_buf) return NULL;
    int64_t t = (int64_t)*tt;
    unix_to_civil(t, tm_buf);
    return tm_buf;
}
