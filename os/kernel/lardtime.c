#include "lardtime.h"
#include "fs.h"
#include "rtc.h"
#include "string.h"

#include <stddef.h>

#define SECS_PER_DAY 86400LL

static lardtime_config_t s_timecfg = {
    540,
    0,
    LARDTIME_VIEW_DANGUN,
    1,
    1,
    LARDTIME_BATTERY_UNKNOWN
};
static int s_timecfg_loaded;

static char lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq_ci(const char* a, const char* b)
{
    uint32_t i = 0;
    if (!a || !b) return 0;
    for (;;) {
        char ca = lower_char(a[i]);
        char cb = lower_char(b[i]);
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
        i++;
    }
}

static void cfg_defaults(void)
{
    s_timecfg.zone_minutes = 540;
    s_timecfg.dst_enabled = 0;
    s_timecfg.default_view = LARDTIME_VIEW_DANGUN;
    s_timecfg.topbar_enabled = 1;
    s_timecfg.battery_enabled = 1;
    s_timecfg.battery_percent = LARDTIME_BATTERY_UNKNOWN;
}

static int parse_bool(const char* word, uint32_t* out)
{
    if (!word || !out) return -1;
    if (streq_ci(word, "on") || streq_ci(word, "yes") || streq_ci(word, "true") ||
        streq_ci(word, "1")) {
        *out = 1u;
        return 0;
    }
    if (streq_ci(word, "off") || streq_ci(word, "no") || streq_ci(word, "false") ||
        streq_ci(word, "0")) {
        *out = 0u;
        return 0;
    }
    return -1;
}

static int parse_zone(const char* word, int32_t* out)
{
    int sign = 1;
    int hh;
    int mm;
    if (!word || !out) return -1;
    if (word[0] == '+') {
        sign = 1;
        word++;
    } else if (word[0] == '-') {
        sign = -1;
        word++;
    }
    if (word[0] < '0' || word[0] > '9' || word[1] < '0' || word[1] > '9') return -1;
    hh = (word[0] - '0') * 10 + (word[1] - '0');
    if (word[2] == ':') {
        if (word[3] < '0' || word[3] > '9' || word[4] < '0' || word[4] > '9' ||
            word[5] != '\0') return -1;
        mm = (word[3] - '0') * 10 + (word[4] - '0');
    } else if (word[2] == '\0') {
        mm = 0;
    } else {
        return -1;
    }
    if (hh > 23 || mm > 59) return -1;
    *out = (int32_t)sign * (int32_t)(hh * 60 + mm);
    return 0;
}

static int read_word_from_file(const uint8_t* data, uint32_t size, uint32_t* pos,
                               char* out, uint32_t cap)
{
    uint32_t i;
    uint32_t n = 0;
    if (!data || !pos || !out || cap == 0) return -1;
    i = *pos;
    while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r')) i++;
    if (i >= size || data[i] == '\n') {
        out[0] = '\0';
        *pos = i;
        return -1;
    }
    while (i < size && data[i] != ' ' && data[i] != '\t' &&
           data[i] != '\r' && data[i] != '\n') {
        if (n + 1u < cap) out[n++] = (char)data[i];
        i++;
    }
    out[n] = '\0';
    *pos = i;
    return 0;
}

static void skip_line(const uint8_t* data, uint32_t size, uint32_t* pos)
{
    uint32_t i = pos ? *pos : 0;
    if (!data || !pos) return;
    while (i < size && data[i] != '\n') i++;
    if (i < size && data[i] == '\n') i++;
    *pos = i;
}

static void apply_cfg_word(const char* key, const char* value)
{
    uint32_t b;
    int32_t z;
    uint32_t view;
    if (!key || !value || !key[0] || !value[0]) return;
    if (streq_ci(key, "zone") || streq_ci(key, "timezone") || streq_ci(key, "timecfg.zone")) {
        if (parse_zone(value, &z) == 0) s_timecfg.zone_minutes = z;
        return;
    }
    if (streq_ci(key, "dst") || streq_ci(key, "daylight")) {
        if (parse_bool(value, &b) == 0) s_timecfg.dst_enabled = b;
        return;
    }
    if (streq_ci(key, "default") || streq_ci(key, "view") || streq_ci(key, "calendar")) {
        if (lardtime_view_from_name(value, &view) == 0) s_timecfg.default_view = view;
        return;
    }
    if (streq_ci(key, "topbar") || streq_ci(key, "deck") || streq_ci(key, "statusbar")) {
        if (parse_bool(value, &b) == 0) s_timecfg.topbar_enabled = b;
        return;
    }
    if (streq_ci(key, "battery") || streq_ci(key, "bat")) {
        if (parse_bool(value, &b) == 0) s_timecfg.battery_enabled = b;
        return;
    }
}

static void load_cfg_from_file(void)
{
    FsWritableFile* f = fs_open_writable("timecfg.lardd");
    uint32_t pos = 0;
    char a[24];
    char b[24];
    char c[24];
    if (!f) return;
    while (pos < f->size) {
        uint32_t line = pos;
        if (read_word_from_file(f->data, f->size, &pos, a, sizeof(a)) == 0) {
            if (streq_ci(a, "SETTING") || streq_ci(a, "SET") || streq_ci(a, "ITEM")) {
                if (read_word_from_file(f->data, f->size, &pos, b, sizeof(b)) == 0 &&
                    read_word_from_file(f->data, f->size, &pos, c, sizeof(c)) == 0) {
                    apply_cfg_word(b, c);
                }
            } else if (read_word_from_file(f->data, f->size, &pos, b, sizeof(b)) == 0) {
                apply_cfg_word(a, b);
            }
        }
        if (pos == line) pos++;
        skip_line(f->data, f->size, &pos);
    }
}

static void ensure_cfg(void)
{
    if (!s_timecfg_loaded) lardtime_config_init();
}

static void fappend(FsWritableFile* f, const char* s)
{
    if (f && s) (void)fs_append(f, (const uint8_t*)s, (uint32_t)strlen(s));
}

static void fappend_u32(FsWritableFile* f, uint32_t v)
{
    char tmp[12];
    uint32_t p = 0;
    uint32_t div = 1000000000u;
    int started = 0;
    while (div > 0) {
        uint32_t d = v / div;
        if (d || started || div == 1u) {
            tmp[p++] = (char)('0' + d);
            started = 1;
        }
        v %= div;
        div /= 10u;
    }
    tmp[p] = '\0';
    fappend(f, tmp);
}

static void fappend_zone(FsWritableFile* f, int32_t minutes)
{
    uint32_t m;
    uint32_t hh;
    uint32_t mm;
    fappend(f, minutes < 0 ? "-" : "+");
    m = minutes < 0 ? (uint32_t)(-minutes) : (uint32_t)minutes;
    hh = m / 60u;
    mm = m % 60u;
    fappend(f, (hh < 10u) ? "0" : "");
    fappend_u32(f, hh);
    fappend(f, ":");
    fappend(f, (mm < 10u) ? "0" : "");
    fappend_u32(f, mm);
}

static void str_append(char* out, uint32_t cap, uint32_t* pos, const char* s)
{
    if (!out || !pos || cap == 0 || !s) return;
    while (*s && *pos + 1u < cap) out[(*pos)++] = *s++;
    out[*pos] = '\0';
}

static void str_append_ch(char* out, uint32_t cap, uint32_t* pos, char c)
{
    if (!out || !pos || cap == 0 || *pos + 1u >= cap) return;
    out[(*pos)++] = c;
    out[*pos] = '\0';
}

static void str_append_u32(char* out, uint32_t cap, uint32_t* pos, uint32_t v)
{
    char tmp[12];
    uint32_t p = 0;
    uint32_t div = 1000000000u;
    int started = 0;
    while (div > 0) {
        uint32_t d = v / div;
        if (d || started || div == 1u) {
            tmp[p++] = (char)('0' + d);
            started = 1;
        }
        v %= div;
        div /= 10u;
    }
    tmp[p] = '\0';
    str_append(out, cap, pos, tmp);
}

static void str_append_2(char* out, uint32_t cap, uint32_t* pos, uint32_t v)
{
    str_append_ch(out, cap, pos, (char)('0' + ((v / 10u) % 10u)));
    str_append_ch(out, cap, pos, (char)('0' + (v % 10u)));
}

static void str_append_year5(char* out, uint32_t cap, uint32_t* pos, uint32_t v)
{
    if (v < 10000u) {
        str_append_ch(out, cap, pos, (char)('0' + ((v / 10000u) % 10u)));
        str_append_ch(out, cap, pos, (char)('0' + ((v / 1000u) % 10u)));
        str_append_ch(out, cap, pos, (char)('0' + ((v / 100u) % 10u)));
        str_append_ch(out, cap, pos, (char)('0' + ((v / 10u) % 10u)));
        str_append_ch(out, cap, pos, (char)('0' + (v % 10u)));
    } else {
        str_append_u32(out, cap, pos, v);
    }
}

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

void lardtime_config_init(void)
{
    cfg_defaults();
    load_cfg_from_file();
    s_timecfg_loaded = 1;
}

int lardtime_config_reload(void)
{
    cfg_defaults();
    load_cfg_from_file();
    s_timecfg_loaded = 1;
    return 0;
}

int lardtime_config_save(void)
{
    FsWritableFile* f;
    ensure_cfg();
    f = fs_open_writable("timecfg.lardd");
    if (!f) return -1;
    f->size = 0;
    fappend(f, "LARDD 1\n");
    fappend(f, "TITLE LardOS Time Config\n");
    fappend(f, "TEXT User-owned global time display policy. Defaults are Korean LardOS-style: Dangun year, UTC+09:00, DST off.\n");
    fappend(f, "TEXT Battery is shown as BAT ? until a power driver reports a real value; LardOS does not fake hardware data.\n");
    fappend(f, "SETTING zone ");
    fappend_zone(f, s_timecfg.zone_minutes);
    fappend(f, "\nSETTING dst ");
    fappend(f, s_timecfg.dst_enabled ? "on" : "off");
    fappend(f, "\nSETTING default ");
    fappend(f, lardtime_view_name(s_timecfg.default_view));
    fappend(f, "\nSETTING topbar ");
    fappend(f, s_timecfg.topbar_enabled ? "on" : "off");
    fappend(f, "\nSETTING battery ");
    fappend(f, s_timecfg.battery_enabled ? "on" : "off");
    fappend(f, "\nSECTION Commands\n");
    fappend(f, "ITEM timecfg status\n");
    fappend(f, "ITEM timecfg zone +09:00\n");
    fappend(f, "ITEM timecfg dst on|off\n");
    fappend(f, "ITEM timecfg default dangun|solar|lunar\n");
    fappend(f, "ITEM timecfg topbar on|off\n");
    fappend(f, "ITEM timecfg battery on|off\n");
    fappend(f, "END\n");
    return 0;
}

void lardtime_config_get(lardtime_config_t* out)
{
    ensure_cfg();
    if (out) *out = s_timecfg;
}

int lardtime_set_zone_minutes(int32_t minutes)
{
    if (minutes < -23 * 60 || minutes > 23 * 60 + 59) return -1;
    ensure_cfg();
    s_timecfg.zone_minutes = minutes;
    return lardtime_config_save();
}

int lardtime_set_dst(int on)
{
    ensure_cfg();
    s_timecfg.dst_enabled = on ? 1u : 0u;
    return lardtime_config_save();
}

int lardtime_set_default_view(uint32_t view)
{
    if (view > LARDTIME_VIEW_LUNAR) return -1;
    ensure_cfg();
    s_timecfg.default_view = view;
    return lardtime_config_save();
}

int lardtime_set_topbar(int on)
{
    ensure_cfg();
    s_timecfg.topbar_enabled = on ? 1u : 0u;
    return lardtime_config_save();
}

int lardtime_set_battery_visible(int on)
{
    ensure_cfg();
    s_timecfg.battery_enabled = on ? 1u : 0u;
    return lardtime_config_save();
}

void lardtime_set_battery_sample(int known, uint8_t percent)
{
    ensure_cfg();
    s_timecfg.battery_percent = known ? (percent > 100u ? 100u : percent) : LARDTIME_BATTERY_UNKNOWN;
}

const char* lardtime_view_name(uint32_t view)
{
    if (view == LARDTIME_VIEW_SOLAR) return "solar";
    if (view == LARDTIME_VIEW_LUNAR) return "lunar";
    return "dangun";
}

int lardtime_view_from_name(const char* name, uint32_t* out)
{
    if (!name || !out) return -1;
    if (streq_ci(name, "solar") || streq_ci(name, "ce") || streq_ci(name, "date")) {
        *out = LARDTIME_VIEW_SOLAR;
        return 0;
    }
    if (streq_ci(name, "dangun") || streq_ci(name, "dan") || streq_ci(name, "default")) {
        *out = LARDTIME_VIEW_DANGUN;
        return 0;
    }
    if (streq_ci(name, "lunar") || streq_ci(name, "moon")) {
        *out = LARDTIME_VIEW_LUNAR;
        return 0;
    }
    return -1;
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

int lardtime_now_configured(lardtime_snapshot_t* out)
{
    int64_t ticks;
    int64_t local_ticks;
    ensure_cfg();
    if (!out) return -1;
    ticks = lardtime_now_ticks();
    if (ticks <= 0) return -2;
    local_ticks = ticks + (int64_t)s_timecfg.zone_minutes * 60LL;
    if (s_timecfg.dst_enabled) local_ticks += 3600LL;
    if (local_ticks < 0 || lardtime_to_civil(local_ticks, &out->civil) != 0) return -3;
    out->ticks = ticks;
    out->dangun_year = lardtime_dangun_year(out->civil.year);
    lardtime_lunar_from_civil(&out->civil, &out->lunar);
    return 0;
}

int lardtime_format_view(const lardtime_snapshot_t* snap, uint32_t view,
                         char* out, uint32_t cap)
{
    uint32_t pos = 0;
    if (!snap || !out || cap == 0 || view > LARDTIME_VIEW_LUNAR) return -1;
    out[0] = '\0';
    if (view == LARDTIME_VIEW_DANGUN) {
        str_append(out, cap, &pos, "Dangun ");
        str_append_year5(out, cap, &pos, snap->dangun_year);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, snap->civil.month);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, snap->civil.day);
    } else if (view == LARDTIME_VIEW_LUNAR) {
        str_append_year5(out, cap, &pos, snap->lunar.year);
        str_append(out, cap, &pos, "-L");
        str_append_2(out, cap, &pos, snap->lunar.month);
        if (snap->lunar.leap_month) str_append(out, cap, &pos, "leap");
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, snap->lunar.day);
    } else {
        str_append_year5(out, cap, &pos, snap->civil.year);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, snap->civil.month);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, snap->civil.day);
    }
    str_append_ch(out, cap, &pos, ' ');
    str_append_2(out, cap, &pos, snap->civil.hour);
    str_append_ch(out, cap, &pos, ':');
    str_append_2(out, cap, &pos, snap->civil.minute);
    str_append_ch(out, cap, &pos, ':');
    str_append_2(out, cap, &pos, snap->civil.second);
    return 0;
}

int lardtime_format_default(char* out, uint32_t cap)
{
    lardtime_snapshot_t now;
    ensure_cfg();
    if (lardtime_now_configured(&now) != 0) return -1;
    return lardtime_format_view(&now, s_timecfg.default_view, out, cap);
}

int lardtime_format_topbar(char* out, uint32_t cap)
{
    lardtime_snapshot_t now;
    uint32_t pos = 0;
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    ensure_cfg();
    if (!s_timecfg.topbar_enabled) return -2;
    if (lardtime_now_configured(&now) != 0) {
        str_append(out, cap, &pos, "TIME ?");
    } else if (s_timecfg.default_view == LARDTIME_VIEW_LUNAR) {
        str_append_ch(out, cap, &pos, 'L');
        str_append_year5(out, cap, &pos, now.lunar.year);
        str_append(out, cap, &pos, "-");
        str_append_2(out, cap, &pos, now.lunar.month);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, now.lunar.day);
        str_append_ch(out, cap, &pos, ' ');
        str_append_2(out, cap, &pos, now.civil.hour);
        str_append_ch(out, cap, &pos, ':');
        str_append_2(out, cap, &pos, now.civil.minute);
    } else if (s_timecfg.default_view == LARDTIME_VIEW_SOLAR) {
        str_append_year5(out, cap, &pos, now.civil.year);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, now.civil.month);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, now.civil.day);
        str_append_ch(out, cap, &pos, ' ');
        str_append_2(out, cap, &pos, now.civil.hour);
        str_append_ch(out, cap, &pos, ':');
        str_append_2(out, cap, &pos, now.civil.minute);
    } else {
        str_append_ch(out, cap, &pos, 'D');
        str_append_year5(out, cap, &pos, now.dangun_year);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, now.civil.month);
        str_append_ch(out, cap, &pos, '-');
        str_append_2(out, cap, &pos, now.civil.day);
        str_append_ch(out, cap, &pos, ' ');
        str_append_2(out, cap, &pos, now.civil.hour);
        str_append_ch(out, cap, &pos, ':');
        str_append_2(out, cap, &pos, now.civil.minute);
    }
    if (s_timecfg.battery_enabled) {
        str_append(out, cap, &pos, " BAT ");
        if (s_timecfg.battery_percent == LARDTIME_BATTERY_UNKNOWN) {
            str_append_ch(out, cap, &pos, '?');
        } else {
            str_append_u32(out, cap, &pos, s_timecfg.battery_percent);
            str_append_ch(out, cap, &pos, '%');
        }
    }
    return 0;
}

int lardtime_selftest(void)
{
    lardtime_civil_t c;
    lardtime_lunar_t l;
    lardtime_snapshot_t snap;
    char buf[64];
    uint32_t view = 99u;
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
    if (lardtime_view_from_name("dangun", &view) != 0 || view != LARDTIME_VIEW_DANGUN) return -7;
    snap.ticks = anchor;
    snap.civil = c;
    snap.dangun_year = lardtime_dangun_year(c.year);
    snap.lunar = l;
    if (lardtime_format_view(&snap, LARDTIME_VIEW_DANGUN, buf, sizeof(buf)) != 0) return -8;
    if (buf[0] != 'D' || buf[7] != '0' || buf[8] != '4') return -9;
    return 0;
}
