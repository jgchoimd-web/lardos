#include "mmu.h"
#include "panic.h"
#include "bootinfo.h"
#include "usermode.h"
#include "cpumode.h"

#include <stdint.h>
#include <stddef.h>

// Linker symbols (physical = virtual for now, identity mapped)
extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __jit_start[];
extern uint8_t __jit_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

// Page table bits
enum {
    PTE_P = 1ull << 0,
    PTE_W = 1ull << 1,
    PTE_U = 1ull << 2,
    PTE_PS = 1ull << 7,
    PTE_G = 1ull << 8,
    PTE_NX = 1ull << 63,
};

static inline uintptr_t align_down(uintptr_t x, uintptr_t a) { return x & ~(a - 1); }
static inline uintptr_t align_up(uintptr_t x, uintptr_t a) { return (x + (a - 1)) & ~(a - 1); }

// Static page tables (identity map)
// - PML4: 1
// - PDPT: 1
// - PD: 4 (maps 0..4GiB using 2MiB pages)
// - PT0: 1 (maps 0..2MiB using 4KiB pages so we can protect kernel in low 2MiB)
// - pt_user_*: 4K PTs for user code and stack regions
static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[4][512] __attribute__((aligned(4096)));
static uint64_t pt0[512] __attribute__((aligned(4096)));
static uint64_t pt_user_code[512] __attribute__((aligned(4096)));
static uint64_t pt_user_stack[512] __attribute__((aligned(4096)));
static int protection_ready;

int mmu_protection_is_ready(void)
{
    return protection_ready;
}

static void zero_page(uint64_t* p)
{
    for (int i = 0; i < 512; i++) p[i] = 0;
}

static void set_pte(uint64_t* pt, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    uint64_t idx = (va >> 12) & 0x1FFu;
    pt[idx] = (pa & 0x000FFFFFFFFFF000ull) | flags;
}

static void map_4k_in_pt0(uintptr_t va, uintptr_t pa, uint64_t flags)
{
    // VA must be < 2MiB
    set_pte(pt0, va, pa, flags | PTE_P);
}

static void fill_identity_pt(uint64_t* pt, uintptr_t base, uint64_t flags)
{
    for (int i = 0; i < 512; i++) {
        uintptr_t va = base + (uintptr_t)i * 0x1000;
        pt[i] = (va & 0x000FFFFFFFFFF000ull) | flags | PTE_P;
    }
}

static void map_range_in_pt0(uintptr_t start, uintptr_t end, uint64_t flags)
{
    uintptr_t va = align_down(start, 0x1000);
    uintptr_t limit = align_up(end, 0x1000);
    if (va >= 0x200000) {
        return;
    }
    if (limit > 0x200000) {
        limit = 0x200000;
    }
    for (; va < limit; va += 0x1000) {
        map_4k_in_pt0(va, va, flags);
    }
}

static void remap_kernel_ranges_in_pt0(void)
{
    // Default: low 2MiB RW + NX (except a few device pages)
    for (uintptr_t va = 0; va < 0x200000; va += 0x1000) {
        map_4k_in_pt0(va, va, PTE_W | PTE_NX);
    }

    // Allow VGA text memory RW (still NX)
    // 0xB8000 is inside low 2MiB, already RW+NX.

    // CPU mode bridge trampoline: copied to low memory and executed briefly.
    map_4k_in_pt0(CPU_MODE_TRAMPOLINE_PA, CPU_MODE_TRAMPOLINE_PA, PTE_W);

    // Stack: top at 0xA0000, grow down. Map 16KiB [0x9C000, 0xA0000), leave 4KiB guard at 0x9B000.
    uintptr_t stack_top = 0xA0000;
    uintptr_t stack_guard = 0x9B000;
    for (uintptr_t va = stack_guard + 0x1000; va < stack_top; va += 0x1000) {
        map_4k_in_pt0(va, va, PTE_W | PTE_NX);
    }
    // Guard page: not present
    pt0[(stack_guard >> 12) & 0x1FFu] = 0;

    // Kernel sections
    uintptr_t t0 = align_down((uintptr_t)__text_start, 0x1000);
    uintptr_t t1 = align_up((uintptr_t)__text_end, 0x1000);
    map_range_in_pt0(t0, t1, 0); // RX (no W, no NX)

    uintptr_t r0 = align_down((uintptr_t)__rodata_start, 0x1000);
    uintptr_t r1 = align_up((uintptr_t)__rodata_end, 0x1000);
    map_range_in_pt0(r0, r1, PTE_NX); // R + NX

    uintptr_t d0 = align_down((uintptr_t)__data_start, 0x1000);
    uintptr_t d1 = align_up((uintptr_t)__data_end, 0x1000);
    map_range_in_pt0(d0, d1, PTE_W | PTE_NX);

    /* JIT region: RWX (W, no NX) so we can execute generated code. */
    uintptr_t j0 = align_down((uintptr_t)__jit_start, 0x1000);
    uintptr_t j1 = align_up((uintptr_t)__jit_end, 0x1000);
    map_range_in_pt0(j0, j1, PTE_W);

    uintptr_t b0 = align_down((uintptr_t)__bss_start, 0x1000);
    uintptr_t b1 = align_up((uintptr_t)__bss_end, 0x1000);
    map_range_in_pt0(b0, b1, PTE_W | PTE_NX);
}

static void map_2m_identity(uint64_t* pdn, uintptr_t base, uint64_t flags)
{
    // base aligned 2MiB, maps base..base+2MiB
    uint64_t idx = (base >> 21) & 0x1FFu;
    pdn[idx] = (base & 0x000FFFFFFFFFF000ull) | flags | PTE_P | PTE_PS;
}

static void build_tables(void)
{
    zero_page(pml4);
    zero_page(pdpt);
    for (int i = 0; i < 4; i++) zero_page(pd[i]);
    zero_page(pt0);

    // PML4[0] -> PDPT
    pml4[0] = ((uintptr_t)pdpt) | PTE_P | PTE_W;

    // PDPT entries 0..3 -> PD tables for 0..4GiB
    for (int i = 0; i < 4; i++) {
        pdpt[i] = ((uintptr_t)pd[i]) | PTE_P | PTE_W;
    }

    // Map 0..4GiB using 2MiB pages, RW + NX by default.
    for (int gi = 0; gi < 4; gi++) {
        uintptr_t base = (uintptr_t)gi * 0x40000000ull; // 1GiB
        for (int j = 0; j < 512; j++) {
            map_2m_identity(pd[gi], base + (uintptr_t)j * 0x200000ull, PTE_W | PTE_NX);
        }
    }

    // Replace the first 2MiB PDE with a 4KiB PT so we can protect kernel + have a guard page.
    remap_kernel_ranges_in_pt0();
    pd[0][0] = ((uintptr_t)pt0) | PTE_P | PTE_W; // PT (no PS)

    // Framebuffer can be above 2MiB; it's covered by the 0..4GiB 2MiB map (RW+NX).
    // If your firmware reports fb_addr >= 4GiB, we'd need more PD tables.
}

void mmu_map_user_region(uintptr_t user_code_va, uintptr_t user_stack_va)
{
    uintptr_t code_base = align_down(user_code_va, 0x200000);
    uintptr_t stack_base = align_down(user_stack_va, 0x200000);
    uint64_t code_pt_idx = (user_code_va >> 21) & 0x1FFu;
    uint64_t code_pte_idx = (user_code_va >> 12) & 0x1FFu;
    uint64_t stack_pt_idx = (user_stack_va >> 21) & 0x1FFu;
    uint64_t stack_pte_idx = (user_stack_va >> 12) & 0x1FFu;

    fill_identity_pt(pt_user_code, code_base, PTE_W | PTE_NX);
    pd[0][code_pt_idx] = ((uintptr_t)pt_user_code) | PTE_P | PTE_W;

    uint64_t* stack_pt = pt_user_code;
    if (stack_pt_idx != code_pt_idx) {
        fill_identity_pt(pt_user_stack, stack_base, PTE_W | PTE_NX);
        pd[0][stack_pt_idx] = ((uintptr_t)pt_user_stack) | PTE_P | PTE_W;
        stack_pt = pt_user_stack;
    }

    // User code: writable while the kernel copies the initial program.
    pt_user_code[code_pte_idx] = (user_code_va & 0x000FFFFFFFFFF000ull) | PTE_P | PTE_W | PTE_U;

    // User stack: 4K at user_stack_va, RW + NX + user-accessible
    stack_pt[stack_pte_idx] = (user_stack_va & 0x000FFFFFFFFFF000ull) | PTE_P | PTE_W | PTE_U | PTE_NX;

    __asm__ __volatile__("invlpg (%0)" : : "r"(user_code_va) : "memory");
    __asm__ __volatile__("invlpg (%0)" : : "r"(user_stack_va) : "memory");
}

void mmu_protect_user_code(uintptr_t user_code_va)
{
    uint64_t code_pte_idx = (user_code_va >> 12) & 0x1FFu;
    pt_user_code[code_pte_idx] &= ~PTE_W;
    __asm__ __volatile__("invlpg (%0)" : : "r"(user_code_va) : "memory");
}

void mmu_map_user_ldll(uintptr_t va)
{
    uint64_t pte_idx = (va >> 12) & 0x1FFu;
    pt_user_code[pte_idx] = (va & 0x000FFFFFFFFFF000ull) | PTE_P | PTE_U;
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
}

#define PF_X 1
#define PF_W 2
#define PF_R 4

void mmu_map_user_segments(const uint32_t* paddrs, const uint32_t* sizes, const uint32_t* flags, int count)
{
    uintptr_t user_base = align_down(USER_VALID_LO, 0x200000);
    uint64_t user_pt_idx = (USER_VALID_LO >> 21) & 0x1FFu;
    fill_identity_pt(pt_user_code, user_base, PTE_W | PTE_NX);
    pd[0][user_pt_idx] = ((uintptr_t)pt_user_code) | PTE_P | PTE_W;

    for (int s = 0; s < count; s++) {
        uint32_t paddr = paddrs[s];
        uint32_t sz = sizes[s];
        uint32_t flg = flags ? flags[s] : (PF_R | PF_X);

        uint32_t va = paddr;
        uint32_t end = paddr + sz;
        if (end < paddr) end = 0xFFFFFFFFu;

        while (va < end) {
            uint64_t pte_idx = (va >> 12) & 0x1FFu;
            uint64_t pte_flags = PTE_P | PTE_U;
            if (flg & PF_W) pte_flags |= PTE_W;
            if (!(flg & PF_X)) pte_flags |= PTE_NX;

            pt_user_code[pte_idx] = (va & 0x000FFFFFFFFFF000ull) | pte_flags;

            __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
            va += 4096;
        }
    }

    /* Always map stack pages for stack and argv. */
    for (uintptr_t stack_va = USER_STACK - 0x1000u; stack_va <= USER_STACK; stack_va += 0x1000u) {
        uint64_t pte_idx = (stack_va >> 12) & 0x1FFu;
        pt_user_code[pte_idx] = (stack_va & 0x000FFFFFFFFFF000ull) | PTE_P | PTE_W | PTE_U | PTE_NX;
        __asm__ __volatile__("invlpg (%0)" : : "r"(stack_va) : "memory");
    }
}

static void switch_cr3(uintptr_t pml4_phys)
{
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void mmu_init_protection(void)
{
    // Fine-grained executable/write-protected sections must fit in PT0.
    if ((uintptr_t)__jit_end >= 0x200000) {
        panic_u64("protected kernel section >=2MiB", (uint64_t)(uintptr_t)__jit_end);
    }

    build_tables();
    switch_cr3((uintptr_t)pml4);
    protection_ready = 1;

    // Optional: global pages on (doesn't change semantics; can help perf)
    uint64_t cr4 = 0;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 7); // PGE
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));

    // TLB flush is implicit via CR3 write above.
}
