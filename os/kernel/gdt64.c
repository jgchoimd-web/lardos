/*
 * 64-bit GDT with kernel code/data, user code/data, and TSS.
 * User segments have DPL=3 so CPL 3 can use them.
 */
#include "gdt64.h"
#include <stdint.h>

/* x86-64 TSS (minimal: need RSP0 for privilege change) */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} tss64_t;

static tss64_t g_tss __attribute__((aligned(16)));

/* GDT entry format (64-bit) */
typedef struct __attribute__((packed)) {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_hi_flags;
    uint8_t base_hi;
} gdt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

/* TSS descriptor (16 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_hi_flags;
    uint8_t base_hi;
    uint32_t base_ext;
    uint32_t reserved;
} gdt_tss_t;

static gdt_entry_t gdt[8];
static gdtr_t gdtr;

extern void gdt64_load(uintptr_t gdtr_addr, uint16_t tss_sel);

void gdt64_init(void)
{
    /* Kernel stack for syscall (TSS.RSP0). Use low-memory stack. */
    g_tss.rsp0 = 0x000000000009E000;
    g_tss.iopb = sizeof(tss64_t);

    /* Null */
    gdt[0] = (gdt_entry_t){0};

    /* Kernel code: DPL 0, 64-bit, execute/read */
    gdt[1].limit_lo = 0xFFFF;
    gdt[1].base_lo = 0;
    gdt[1].base_mid = 0;
    gdt[1].access = 0x9A;
    gdt[1].limit_hi_flags = 0xAF;
    gdt[1].base_hi = 0;

    /* Kernel data: DPL 0, 64-bit, read/write */
    gdt[2].limit_lo = 0xFFFF;
    gdt[2].base_lo = 0;
    gdt[2].base_mid = 0;
    gdt[2].access = 0x92;
    gdt[2].limit_hi_flags = 0xCF;
    gdt[2].base_hi = 0;

    /* Long/64-bit code: same as boot, so CS=0x18 remains valid */
    gdt[3].limit_lo = 0xFFFF;
    gdt[3].base_lo = 0;
    gdt[3].base_mid = 0;
    gdt[3].access = 0x9A;
    gdt[3].limit_hi_flags = 0xAF;
    gdt[3].base_hi = 0;

    /* User code: DPL 3, 64-bit, execute/read */
    gdt[4].limit_lo = 0xFFFF;
    gdt[4].base_lo = 0;
    gdt[4].base_mid = 0;
    gdt[4].access = 0xFA;
    gdt[4].limit_hi_flags = 0xAF;
    gdt[4].base_hi = 0;

    /* User data: DPL 3, 64-bit, read/write */
    gdt[5].limit_lo = 0xFFFF;
    gdt[5].base_lo = 0;
    gdt[5].base_mid = 0;
    gdt[5].access = 0xF2;
    gdt[5].limit_hi_flags = 0xCF;
    gdt[5].base_hi = 0;

    /* TSS (index 6) - 16-byte descriptor */
    uint64_t tss_base = (uint64_t)(uintptr_t)&g_tss;
    gdt[6].limit_lo = sizeof(tss64_t) - 1;
    gdt[6].base_lo = (uint16_t)(tss_base & 0xFFFF);
    gdt[6].base_mid = (uint8_t)((tss_base >> 16) & 0xFF);
    gdt[6].access = 0x89;
    gdt[6].limit_hi_flags = 0x40;
    gdt[6].base_hi = (uint8_t)((tss_base >> 24) & 0xFF);
    ((uint32_t*)(&gdt[6]))[2] = (uint32_t)(tss_base >> 32);
    ((uint32_t*)(&gdt[6]))[3] = 0;

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base = (uint64_t)(uintptr_t)gdt;

    gdt64_load((uintptr_t)&gdtr, GDT64_SEL_TSS);
}
