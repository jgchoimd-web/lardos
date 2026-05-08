#include "awake.h"

#include <stdint.h>

static awake_info_t s_awake;

static void scopy(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void awake_init(void)
{
    for (uint32_t i = 0; i < sizeof(s_awake); i++) ((uint8_t*)&s_awake)[i] = 0;
    scopy(s_awake.current, sizeof(s_awake.current), "off");
}

void awake_enable(int enabled, uint32_t total)
{
    awake_init();
    if (!enabled) return;
    s_awake.enabled = 1u;
    s_awake.total = total ? total : 1u;
    scopy(s_awake.current, sizeof(s_awake.current), "ready");
}

void awake_mark(uint32_t phase, const char* current)
{
    if (!s_awake.enabled) return;
    s_awake.phase = phase;
    s_awake.background_runs++;
    if (phase > s_awake.total) s_awake.total = phase;
    scopy(s_awake.current, sizeof(s_awake.current), current);
}

void awake_finish(void)
{
    if (!s_awake.enabled) return;
    s_awake.done = 1u;
    s_awake.phase = s_awake.total;
    scopy(s_awake.current, sizeof(s_awake.current), "complete");
}

void awake_fail(uint32_t error, const char* current)
{
    if (!s_awake.enabled) return;
    s_awake.last_error = error ? error : 1u;
    scopy(s_awake.current, sizeof(s_awake.current), current);
}

void awake_info(awake_info_t* out)
{
    if (!out) return;
    *out = s_awake;
}

int awake_selftest(void)
{
    awake_info_t saved = s_awake;
    awake_info_t info;
    awake_enable(1, 3u);
    awake_mark(1u, "drivers");
    awake_info(&info);
    if (!info.enabled || info.phase != 1u || info.total != 3u || info.background_runs != 1u) {
        s_awake = saved;
        return -1;
    }
    awake_finish();
    awake_info(&info);
    if (!info.done || info.phase != 3u || info.current[0] != 'c') {
        s_awake = saved;
        return -2;
    }
    s_awake = saved;
    return 0;
}
