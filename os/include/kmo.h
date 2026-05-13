#pragma once

#include <stdint.h>

#define KMO_MAX_MODULES 12
#define KMO_BODY_MAX 512u

typedef struct kmo_module {
    int used;
    int writable;
    char file[32];
    char id[24];
    char name[32];
    char target[24];
    char help[96];
    char default_msg[96];
    char body[KMO_BODY_MAX];
} kmo_module_t;

void kmo_reset(void);
uint32_t kmo_reload(void);
uint32_t kmo_count(void);
const kmo_module_t* kmo_get(uint32_t index);
const kmo_module_t* kmo_find(const char* key, uint32_t* index_out);
int kmo_load_file(const char* name);
int kmo_format(const char* key, char* out, uint32_t out_cap);
int kmo_run(const char* key, const char* message, char* out, uint32_t out_cap);
int kmo_create(const char* name, const char* target, const char* default_msg);
int kmo_set_field(const char* name, const char* field, const char* value);
int kmo_delete(const char* name);
int kmo_selftest(void);
