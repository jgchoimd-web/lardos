/*
 * SMP: MP 테이블 파싱, 코어 수 감지, AP 웨이크.
 * 코어가 3개 이상이면 코어 1에서 보조 모놀리식 커널(aux)을 실행.
 */
#include "smp.h"
#include <stddef.h>

/* Local APIC MMIO (물리 주소) */
#define LAPIC_BASE   0xFEE00000u
#define LAPIC_ID     (LAPIC_BASE + 0x020)
#define LAPIC_ICR_LO (LAPIC_BASE + 0x300)
#define LAPIC_ICR_HI (LAPIC_BASE + 0x310)

#define LAPIC_SVR    (LAPIC_BASE + 0x0F0)
#define LAPIC_SVR_EN (1u << 8)

#define AP_TRAMPOLINE_PA  0x4000
#define AUX_KERNEL_PA     0x200000

static inline uint32_t lapic_read(uintptr_t off)
{
    return *(volatile uint32_t*)(off);
}

static inline void lapic_write(uintptr_t off, uint32_t val)
{
    *(volatile uint32_t*)(off) = val;
}

static void lapic_wait_ready(void)
{
    while (lapic_read(LAPIC_ICR_LO) & (1u << 12))
        ;
}

/* MP Floating Pointer: "_MP_" 시그니처 */
#define MP_SIGNATURE 0x5F504D5Fu  /* "_MP_" */

struct mp_floating_ptr {
    uint32_t signature;
    uint32_t phys_addr;
    uint8_t  length;
    uint8_t  spec_rev;
    uint8_t  checksum;
    uint8_t  feature[5];
} __attribute__((packed));

/* MP Config Table processor entry type */
#define MP_PROCESSOR 0

struct mp_processor {
    uint8_t type;
    uint8_t apic_id;
    uint8_t apic_ver;
    uint8_t cpu_flags;  /* bit 0: BSP */
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint64_t reserved[2];
} __attribute__((packed));

static const struct mp_floating_ptr* find_mp_fp(void)
{
    /* EBDA 마지막 1KB (0x9FC00-0x9FFFF) */
    for (uintptr_t p = 0x9FC00; p < 0xA0000; p += 16) {
        const struct mp_floating_ptr* fp = (const struct mp_floating_ptr*)p;
        if (fp->signature == MP_SIGNATURE)
            return fp;
    }
    /* BIOS ROM 0xF0000-0xFFFFF */
    for (uintptr_t p = 0xF0000; p < 0x100000; p += 16) {
        const struct mp_floating_ptr* fp = (const struct mp_floating_ptr*)p;
        if (fp->signature == MP_SIGNATURE)
            return fp;
    }
    return NULL;
}

#define MP_CONFIG_SIG 0x504D4350u  /* "PCMP" */

static int count_cpus_from_mp_table(void)
{
    const struct mp_floating_ptr* fp = find_mp_fp();
    if (!fp)
        return 1;

    const uint8_t* cfg = (const uint8_t*)(uintptr_t)fp->phys_addr;
    if ((uintptr_t)cfg < 0x10000)
        return 1;
    if (*(const uint32_t*)cfg != MP_CONFIG_SIG)
        return 1;

    /* MP Config: entry_count @ 36, entries @ 46 */
    uint16_t entry_count = *(const uint16_t*)(cfg + 36);
    const uint8_t* entries = cfg + 46;

    int cpu_count = 0;
    for (int i = 0; i < 256 && i < entry_count; i++) {
        uint8_t type = entries[0];
        if (type == MP_PROCESSOR) {
            cpu_count++;
            entries += 20;
        } else if (type == 1) {
            entries += 8;
        } else if (type == 2) {
            entries += 8;
        } else if (type == 3) {
            entries += 8;
        } else if (type == 4) {
            entries += 8;
        } else {
            break;
        }
    }
    return cpu_count > 0 ? cpu_count : 1;
}

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];
extern uint8_t ap_gdt[];
extern uint8_t ap_gdtr[];
extern uint8_t aux_kernel_start[];
extern uint8_t aux_kernel_end[];

static void copy_trampoline(void)
{
    size_t len = (size_t)(ap_trampoline_end - ap_trampoline_start);
    if (len > 4096)
        len = 4096;
    for (size_t i = 0; i < len; i++)
        ((uint8_t*)AP_TRAMPOLINE_PA)[i] = ap_trampoline_start[i];
    /* GDTR base를 실행 주소(0x4000) 기준으로 패치 */
    ptrdiff_t gdtr_off = (uint8_t*)ap_gdtr - (uint8_t*)ap_trampoline_start;
    ptrdiff_t gdt_off = (uint8_t*)ap_gdt - (uint8_t*)ap_trampoline_start;
    *(uint32_t*)(AP_TRAMPOLINE_PA + gdtr_off + 2) = (uint32_t)(AP_TRAMPOLINE_PA + gdt_off);
}

static void copy_aux_kernel(void)
{
    size_t len = (size_t)(aux_kernel_end - aux_kernel_start);
    if (len > 65536)
        len = 65536;
    for (size_t i = 0; i < len; i++)
        ((uint8_t*)AUX_KERNEL_PA)[i] = aux_kernel_start[i];
}

void smp_init(void)
{
    int ncpu = count_cpus_from_mp_table();
    if (ncpu < 3)
        return;

    /* LAPIC 활성화 (혹시 꺼져 있다면) */
    uint32_t svr = lapic_read(LAPIC_SVR);
    lapic_write(LAPIC_SVR, svr | LAPIC_SVR_EN);

    copy_trampoline();
    copy_aux_kernel();

    /* INIT-SIPI-SIPI: 모든 AP에게 IPI 전송 */
    lapic_wait_ready();
    lapic_write(LAPIC_ICR_HI, 0xFF << 24); /* dest: all excluding self */
    lapic_write(LAPIC_ICR_LO, (4 << 8) | (3 << 18)); /* INIT, all excl self, level trigger */
    lapic_wait_ready();

    /* 10ms 대기 (INIT 이후) - 간단히 busy loop */
    for (volatile int i = 0; i < 10000000; i++)
        ;

    /* 1차 SIPI: vector 0x40 → 물리 주소 0x4000 */
    lapic_wait_ready();
    lapic_write(LAPIC_ICR_HI, 0xFF << 24);
    lapic_write(LAPIC_ICR_LO, (uint32_t)0x40 | (5 << 8) | (3 << 18));
    lapic_wait_ready();

    for (volatile int i = 0; i < 10000; i++)
        ;

    /* 2차 SIPI (일부 CPU는 첫 SIPI에서 깨지지 않을 수 있음) */
    lapic_wait_ready();
    lapic_write(LAPIC_ICR_HI, 0xFF << 24);
    lapic_write(LAPIC_ICR_LO, (uint32_t)0x40 | (5 << 8) | (3 << 18));
    lapic_wait_ready();
}
