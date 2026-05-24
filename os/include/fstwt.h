#pragma once

#include <stdint.h>

#define FSTWT_SCRIPT_MAX 2048u
#define FSTWT_NAME_MAX 32u

typedef struct {
    uint32_t active;
    uint32_t script_bytes;
    uint32_t rules;
    uint32_t to_hits;
    uint32_t from_hits;
    uint32_t misses;
    uint32_t last_error;
    char source[FSTWT_NAME_MAX];
} fstwt_info_t;

void fstwt_init(void);
int fstwt_load_script(const uint8_t* data, uint32_t len, const char* source);
void fstwt_clear(void);
int fstwt_translate_to_lard(const char* external_path, char* out, uint32_t cap);
int fstwt_translate_from_lard(const char* lard_name, char* out, uint32_t cap);
void fstwt_info(fstwt_info_t* out);
const char* fstwt_script(void);
uint32_t fstwt_script_size(void);
int fstwt_selftest(void);
