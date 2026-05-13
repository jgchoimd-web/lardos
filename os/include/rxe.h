#pragma once

#include <stdint.h>
#include "sysrxe.h"

#define RXE_MAX_APPS 8
#define RXE_APP_BASE (SYSRXE_APP_BASE + SYSRXE_MAX_APPS)

typedef sysrxe_app_t rxe_app_t;

void rxe_reset(void);
uint32_t rxe_reload(void);
uint32_t rxe_count(void);
const rxe_app_t* rxe_get(uint32_t index);
const rxe_app_t* rxe_get_by_app(int app);
int rxe_is_app(int app);
int rxe_app_id(uint32_t index);
int rxe_index_from_app(int app);
int rxe_load_file(const char* name);
int rxe_format_home(int app, char* out, uint32_t out_cap);
int rxe_run(int app, const char* input, char* out, uint32_t out_cap);
int rxe_is_game(int app);
int rxe_selftest(void);
