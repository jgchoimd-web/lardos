#pragma once

#include <stdint.h>

#define CPU_MODE_TRAMPOLINE_PA 0x6000u
#define CPU_MODE_TRAMPOLINE_MAX 4096u

typedef struct {
    uint32_t bridge_ready;
    uint32_t long_mode_active;
    uint32_t trampoline_pa;
    uint32_t trampoline_size;
    uint32_t roundtrip_count;
    uint32_t last_roundtrip_ok;
    uint32_t last_error;
} cpu_mode_info_t;

void cpu_mode_init(void);
int cpu_mode_bridge_ready(void);
int cpu_mode_long_mode_active(void);
int cpu_mode_roundtrip_probe(void);
void cpu_mode_info(cpu_mode_info_t* out);
const char* cpu_mode_current_name(void);
