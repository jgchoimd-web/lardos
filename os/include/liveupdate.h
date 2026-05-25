#pragma once

#include <stdint.h>

#define LIVEUPDATE_DETAIL_MAX 96u
#define LIVEUPDATE_NAME_MAX 31u

#define LIVEUPDATE_FLAG_APPEND 0x01u
#define LIVEUPDATE_FLAG_RELOAD 0x02u
#define LIVEUPDATE_FLAG_DECODE 0x04u

typedef struct {
    uint32_t generation;
    uint32_t writes;
    uint32_t reloads;
    uint32_t failures;
    uint32_t auto_enabled;
    int32_t last_result;
    char last_file[LIVEUPDATE_NAME_MAX + 1u];
    char last_scope[16];
    char last_detail[LIVEUPDATE_DETAIL_MAX + 1u];
} liveupdate_info_t;

void liveupdate_init(void);
void liveupdate_info(liveupdate_info_t* out);
int liveupdate_set_auto(int on);
int liveupdate_apply_text(const char* name, const char* text, uint32_t flags,
                          char* out, uint32_t out_cap);
int liveupdate_apply_from_file(const char* src, const char* dst, uint32_t flags,
                               char* out, uint32_t out_cap);
int liveupdate_reload(const char* scope, char* out, uint32_t out_cap);
int liveupdate_selftest(void);
