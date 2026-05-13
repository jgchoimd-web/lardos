#pragma once

#include <stdint.h>

#define SYSRXE_MAX_APPS 8
#define SYSRXE_APP_BASE 10

typedef struct sysrxe_app {
    int used;
    char file[32];
    char id[24];
    char name[24];
    char icon[4];
    uint32_t color;
    char input_label[24];
    char button_label[24];
    char body[1024];
    char command[128];
    int show_desktop;
    int show_dock;
} sysrxe_app_t;

void sysrxe_reset(void);
uint32_t sysrxe_reload(void);
uint32_t sysrxe_count(void);
const sysrxe_app_t* sysrxe_get(uint32_t index);
const sysrxe_app_t* sysrxe_get_by_app(int app);
int sysrxe_is_app(int app);
int sysrxe_app_id(uint32_t index);
int sysrxe_index_from_app(int app);
int sysrxe_load_file(const char* name);
int sysrxe_format_home(int app, char* out, uint32_t out_cap);
int sysrxe_run(int app, const char* input, char* out, uint32_t out_cap);
int sysrxe_selftest(void);
