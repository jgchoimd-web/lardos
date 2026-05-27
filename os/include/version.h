#pragma once

#include "version_gen.h"

#ifndef LARDOS_VERSION
#define LARDOS_VERSION "v0.0.0b"
#endif

#ifndef LARDOS_LTS_NAME
#define LARDOS_LTS_NAME ""
#endif

#ifndef LARDOS_LTS_ACTIVE
#define LARDOS_LTS_ACTIVE 0
#endif

#ifndef LARDOS_HARDWARE_PROFILE
#define LARDOS_HARDWARE_PROFILE "universal"
#endif

static inline int lardos_release_channel_char(char c)
{
    return c == 'a' || c == 'b' || c == 'p';
}

static inline char lardos_release_channel_suffix(const char* version)
{
    char suffix = '?';
    unsigned int i = 0;
    if (!version) return suffix;
    while (version[i]) {
        char c = version[i];
        char prev = i ? version[i - 1u] : '\0';
        char next = version[i + 1u];
        if (lardos_release_channel_char(c) &&
            ((prev >= '0' && prev <= '9') || prev == '.') &&
            (next == '\0' || next == '-' || next == '+' || next == ' ')) {
            suffix = c;
        }
        i++;
    }
    return suffix;
}

static inline const char* lardos_release_channel_name(void)
{
    char suffix = lardos_release_channel_suffix(LARDOS_VERSION);
    if (suffix == 'a') return "official";
    if (suffix == 'b') return "beta-experimental";
    if (suffix == 'p') return "hotpatch";
    return "unknown";
}
