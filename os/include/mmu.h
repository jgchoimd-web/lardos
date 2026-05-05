#pragma once

// Builds new page tables and enables protections:
// - kernel .text: RX (no write)
// - kernel .rodata: R + NX
// - kernel .data/.bss: RW + NX
// - stack: RW + NX with an unmapped guard page below it
void mmu_init_protection(void);

// Map user-mode pages (PTE_U). Call after mmu_init_protection.
// user_code_va: 4K at va, RX
// user_stack_va: 4K at va, RW + NX
void mmu_map_user_region(uintptr_t user_code_va, uintptr_t user_stack_va);

// Map one more user page for LDLL at va (RX, PTE_U)
void mmu_map_user_ldll(uintptr_t va);

// Map user segments for LARDX loader. paddrs/sizes/flags arrays, count segments.
// flags: PF_X=1, PF_W=2, PF_R=4 (from LARDX phdr)
void mmu_map_user_segments(const uint32_t* paddrs, const uint32_t* sizes, const uint32_t* flags, int count);

