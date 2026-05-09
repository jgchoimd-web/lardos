#pragma once

#include "time.h"
#include <stdint.h>

/* CMOS RTC -> Unix seconds (UTC), kept as an internal compatibility input. */
int64_t rtc_unix_seconds(void);

/* Freestanding time hook. */
time_t rtc_time(time_t* tp);

struct tm* rtc_gmtime_r(const time_t* tt, struct tm* tm_buf);
