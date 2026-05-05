#pragma once

#include "time.h"
#include <stdint.h>

/* CMOS RTC -> Unix seconds (UTC). Returns 0 if RTC is invalid/unreadable. */
int64_t rtc_unix_seconds(void);

/* mbedTLS platform hook (mbedtls_time_t is time_t). */
time_t rtc_mbedtls_time(time_t* tp);

/* mbedtls_platform_gmtime_r replacement when MBEDTLS_PLATFORM_GMTIME_R_ALT defined. */
struct tm* mbedtls_platform_gmtime_r(const time_t* tt, struct tm* tm_buf);
