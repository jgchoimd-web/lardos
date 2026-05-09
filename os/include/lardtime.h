#pragma once

#include <stdint.h>

#define LARDTIME_YEAR_WIDTH 5u
#define LARDTIME_EPOCH_YEAR 0u
#define LARDTIME_DANGUN_OFFSET 2333u

typedef struct {
    uint32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t yday;
    uint8_t wday;
} lardtime_civil_t;

typedef struct {
    uint32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t leap_month;
} lardtime_lunar_t;

typedef struct {
    int64_t ticks;
    lardtime_civil_t civil;
    lardtime_lunar_t lunar;
    uint32_t dangun_year;
} lardtime_snapshot_t;

int64_t lardtime_from_civil(uint32_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute, uint8_t second);
int lardtime_to_civil(int64_t ticks, lardtime_civil_t* out);
int64_t lardtime_unix_to_ticks(int64_t unix_seconds);
int64_t lardtime_now_ticks(void);
int lardtime_now(lardtime_snapshot_t* out);
void lardtime_lunar_from_civil(const lardtime_civil_t* civil, lardtime_lunar_t* out);
uint32_t lardtime_dangun_year(uint32_t civil_year);
int lardtime_selftest(void);
