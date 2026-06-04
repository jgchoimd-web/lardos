#pragma once

#include <stdint.h>

#define LARDTIME_YEAR_WIDTH 5u
#define LARDTIME_EPOCH_YEAR 0u
#define LARDTIME_DANGUN_OFFSET 2333u

#define LARDTIME_VIEW_SOLAR  0u
#define LARDTIME_VIEW_DANGUN 1u
#define LARDTIME_VIEW_LUNAR  2u

#define LARDTIME_BATTERY_UNKNOWN 0xFFu

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

typedef struct {
    int32_t zone_minutes;
    uint32_t dst_enabled;
    uint32_t default_view;
    uint32_t topbar_enabled;
    uint32_t battery_enabled;
    uint8_t battery_percent;
} lardtime_config_t;

int64_t lardtime_from_civil(uint32_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute, uint8_t second);
int lardtime_to_civil(int64_t ticks, lardtime_civil_t* out);
int64_t lardtime_unix_to_ticks(int64_t unix_seconds);
int64_t lardtime_now_ticks(void);
int lardtime_now(lardtime_snapshot_t* out);
void lardtime_config_init(void);
int lardtime_config_reload(void);
int lardtime_config_save(void);
void lardtime_config_get(lardtime_config_t* out);
int lardtime_set_zone_minutes(int32_t minutes);
int lardtime_set_dst(int on);
int lardtime_set_default_view(uint32_t view);
int lardtime_set_topbar(int on);
int lardtime_set_battery_visible(int on);
void lardtime_set_battery_sample(int known, uint8_t percent);
const char* lardtime_view_name(uint32_t view);
int lardtime_view_from_name(const char* name, uint32_t* out);
int lardtime_now_configured(lardtime_snapshot_t* out);
int lardtime_format_view(const lardtime_snapshot_t* snap, uint32_t view,
                         char* out, uint32_t cap);
int lardtime_format_default(char* out, uint32_t cap);
int lardtime_format_topbar(char* out, uint32_t cap);
void lardtime_lunar_from_civil(const lardtime_civil_t* civil, lardtime_lunar_t* out);
uint32_t lardtime_dangun_year(uint32_t civil_year);
int lardtime_selftest(void);
