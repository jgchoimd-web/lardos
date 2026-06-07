#pragma once

#include <stdint.h>

#include "os_vm.h"

#define LHA_VM_MAX       4u
#define LHA_NAME_MAX     24u
#define LHA_PROGRAM_MAX  512u

typedef struct {
    char name[LHA_NAME_MAX];
    uint32_t slot;
    uint32_t used;
    uint32_t runs;
    uint32_t failures;
    uint32_t source_size;
    int32_t last_rc;
} lha_info_t;

const char* lha_last_error(void);
void lha_clear(void);
uint32_t lha_count(void);
int lha_info(uint32_t slot, lha_info_t* out);
int lha_create(const char* name, const char* osvm_source, uint32_t* out_slot);
int lha_create_from_text(const char* fallback_name, const uint8_t* text, uint32_t len,
                         uint32_t* out_slot);
int lha_run(uint32_t slot, os_vm_putc_fn putc, void* user);
int lha_run_source(const char* name, const char* osvm_source, os_vm_putc_fn putc, void* user);
int lha_run_file(const char* path, os_vm_putc_fn putc, void* user);
int lha_demo(os_vm_putc_fn putc, void* user);
int lha_selftest(void);
