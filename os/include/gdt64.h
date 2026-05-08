#pragma once

/* Initialize GDT with kernel code/data, user code/data, and TSS.
 * Must be called before idt64_init (TSS. RSP0 used by syscall). */
void gdt64_init(void);

/* Selectors. Indices 1-3 match boot GDT for continuity. */
#define GDT64_SEL_KCODE  0x08   /* index 1 */
#define GDT64_SEL_KDATA  0x10   /* index 2 */
#define GDT64_SEL_LCODE  0x18   /* index 3, 64-bit (boot uses this for CS) */
#define GDT64_SEL_UCODE  0x23   /* index 4, RPL 3 */
#define GDT64_SEL_UDATA  0x2B   /* index 5, RPL 3 */
#define GDT64_SEL_TSS    0x30   /* index 6 */
#define GDT64_SEL_PCODE32 0x40  /* index 8, 32-bit protected-mode bridge */
