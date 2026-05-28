#include "cpumode.h"

#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

#define IA32_EFER 0xC0000080u
#define EFER_LMA  (1ull << 10)

enum {
    CPU_MODE_ERR_NONE = 0,
    CPU_MODE_ERR_MMU_NOT_READY = 1,
    CPU_MODE_ERR_TRAMPOLINE_TOO_BIG = 2,
    CPU_MODE_ERR_BRIDGE_NOT_READY = 3,
    CPU_MODE_ERR_ROUNDTRIP_FAILED = 4,
    CPU_MODE_ERR_NOT_LONG_AFTER_RETURN = 5,
};

extern uint8_t cpu_mode_trampoline_start[];
extern uint8_t cpu_mode_trampoline_end[];
extern uint8_t cpu_mode_real16_aux_marker[];
extern int cpu_mode_enter_real_probe_asm(void);
extern int cpu_mode_enter_panicroom_texture_asm(void);
extern int cpu_mode_enter_auxkernel_real16_asm(void);

static uint32_t s_bridge_ready;
static uint32_t s_trampoline_size;
static uint32_t s_roundtrip_count;
static uint32_t s_last_roundtrip_ok;
static uint32_t s_panicroom_texture_count;
static uint32_t s_last_panicroom_texture_ok;
static uint32_t s_auxkernel_real16_count;
static uint32_t s_last_auxkernel_real16_ok;
static uint32_t s_last_auxkernel_real16_marker;
static uint32_t s_last_error;

static uint64_t read_efer(void)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_EFER));
    return ((uint64_t)hi << 32) | lo;
}

int cpu_mode_long_mode_active(void)
{
    return (read_efer() & EFER_LMA) != 0;
}

const char* cpu_mode_current_name(void)
{
    return cpu_mode_long_mode_active() ? "long64" : "not-long64";
}

void cpu_mode_init(void)
{
    size_t len = (size_t)(cpu_mode_trampoline_end - cpu_mode_trampoline_start);
    volatile uint8_t* dst = (volatile uint8_t*)(uintptr_t)CPU_MODE_TRAMPOLINE_PA;

    s_bridge_ready = 0;
    s_trampoline_size = (uint32_t)len;
    s_last_roundtrip_ok = 0;
    s_last_panicroom_texture_ok = 0;
    s_last_auxkernel_real16_ok = 0;
    s_last_auxkernel_real16_marker = 0;

    if (!mmu_protection_is_ready()) {
        s_last_error = CPU_MODE_ERR_MMU_NOT_READY;
        return;
    }
    if (len == 0 || len > CPU_MODE_TRAMPOLINE_MAX) {
        s_last_error = CPU_MODE_ERR_TRAMPOLINE_TOO_BIG;
        return;
    }

    for (size_t i = 0; i < len; i++) {
        dst[i] = cpu_mode_trampoline_start[i];
    }
    __asm__ __volatile__("mfence" ::: "memory");

    s_bridge_ready = 1;
    s_last_error = CPU_MODE_ERR_NONE;
}

int cpu_mode_bridge_ready(void)
{
    return s_bridge_ready != 0;
}

int cpu_mode_roundtrip_probe(void)
{
    int r;
    if (!s_bridge_ready) {
        s_last_error = CPU_MODE_ERR_BRIDGE_NOT_READY;
        s_last_roundtrip_ok = 0;
        return -1;
    }

    r = cpu_mode_enter_real_probe_asm();
    s_roundtrip_count++;
    if (r != 1) {
        s_last_error = CPU_MODE_ERR_ROUNDTRIP_FAILED;
        s_last_roundtrip_ok = 0;
        return -1;
    }
    if (!cpu_mode_long_mode_active()) {
        s_last_error = CPU_MODE_ERR_NOT_LONG_AFTER_RETURN;
        s_last_roundtrip_ok = 0;
        return -1;
    }

    s_last_error = CPU_MODE_ERR_NONE;
    s_last_roundtrip_ok = 1;
    return 0;
}

int cpu_mode_panicroom_texture(void)
{
    int r;
    if (!s_bridge_ready) {
        s_last_error = CPU_MODE_ERR_BRIDGE_NOT_READY;
        s_last_panicroom_texture_ok = 0;
        return -1;
    }

    r = cpu_mode_enter_panicroom_texture_asm();
    s_roundtrip_count++;
    s_panicroom_texture_count++;
    if (r != 1) {
        s_last_error = CPU_MODE_ERR_ROUNDTRIP_FAILED;
        s_last_panicroom_texture_ok = 0;
        return -1;
    }
    if (!cpu_mode_long_mode_active()) {
        s_last_error = CPU_MODE_ERR_NOT_LONG_AFTER_RETURN;
        s_last_panicroom_texture_ok = 0;
        return -1;
    }

    s_last_error = CPU_MODE_ERR_NONE;
    s_last_roundtrip_ok = 1;
    s_last_panicroom_texture_ok = 1;
    return 0;
}

int cpu_mode_auxkernel_real16_probe(void)
{
    int r;
    uint32_t marker_off;
    volatile uint8_t* marker;
    if (!s_bridge_ready) {
        s_last_error = CPU_MODE_ERR_BRIDGE_NOT_READY;
        s_last_auxkernel_real16_ok = 0;
        return -1;
    }

    marker_off = (uint32_t)(cpu_mode_real16_aux_marker - cpu_mode_trampoline_start);
    marker = (volatile uint8_t*)(uintptr_t)(CPU_MODE_TRAMPOLINE_PA + marker_off);
    marker[0] = 0;
    marker[1] = 0;
    marker[2] = 0;
    marker[3] = 0;

    r = cpu_mode_enter_auxkernel_real16_asm();
    s_roundtrip_count++;
    s_auxkernel_real16_count++;
    s_last_auxkernel_real16_marker = ((uint32_t)marker[0]) |
                                     ((uint32_t)marker[1] << 8) |
                                     ((uint32_t)marker[2] << 16) |
                                     ((uint32_t)marker[3] << 24);
    if (r != 1 || marker[0] != 0xA1u || marker[1] != 0x16u ||
        (((uint32_t)marker[2] | (uint32_t)marker[3]) == 0u)) {
        s_last_error = CPU_MODE_ERR_ROUNDTRIP_FAILED;
        s_last_auxkernel_real16_ok = 0;
        return -1;
    }
    if (!cpu_mode_long_mode_active()) {
        s_last_error = CPU_MODE_ERR_NOT_LONG_AFTER_RETURN;
        s_last_auxkernel_real16_ok = 0;
        return -1;
    }

    s_last_error = CPU_MODE_ERR_NONE;
    s_last_roundtrip_ok = 1;
    s_last_auxkernel_real16_ok = 1;
    return 0;
}

void cpu_mode_info(cpu_mode_info_t* out)
{
    if (!out) return;
    out->bridge_ready = s_bridge_ready;
    out->long_mode_active = (uint32_t)cpu_mode_long_mode_active();
    out->trampoline_pa = CPU_MODE_TRAMPOLINE_PA;
    out->trampoline_size = s_trampoline_size;
    out->roundtrip_count = s_roundtrip_count;
    out->last_roundtrip_ok = s_last_roundtrip_ok;
    out->panicroom_texture_count = s_panicroom_texture_count;
    out->last_panicroom_texture_ok = s_last_panicroom_texture_ok;
    out->auxkernel_real16_count = s_auxkernel_real16_count;
    out->last_auxkernel_real16_ok = s_last_auxkernel_real16_ok;
    out->last_auxkernel_real16_marker = s_last_auxkernel_real16_marker;
    out->last_error = s_last_error;
}
