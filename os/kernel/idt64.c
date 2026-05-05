#include "idt64.h"
#include "panic.h"
#include "syscall.h"

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

// Assembly stubs
extern void isr64_stub_0(void);
extern void isr64_stub_1(void);
extern void isr64_stub_2(void);
extern void isr64_stub_3(void);
extern void isr64_stub_4(void);
extern void isr64_stub_5(void);
extern void isr64_stub_6(void);
extern void isr64_stub_7(void);
extern void isr64_stub_8(void);
extern void isr64_stub_9(void);
extern void isr64_stub_10(void);
extern void isr64_stub_11(void);
extern void isr64_stub_12(void);
extern void isr64_stub_13(void);
extern void isr64_stub_14(void);
extern void isr64_stub_15(void);
extern void isr64_stub_16(void);
extern void isr64_stub_17(void);
extern void isr64_stub_18(void);
extern void isr64_stub_19(void);
extern void isr64_stub_20(void);
extern void isr64_stub_21(void);
extern void isr64_stub_22(void);
extern void isr64_stub_23(void);
extern void isr64_stub_24(void);
extern void isr64_stub_25(void);
extern void isr64_stub_26(void);
extern void isr64_stub_27(void);
extern void isr64_stub_28(void);
extern void isr64_stub_29(void);
extern void isr64_stub_30(void);
extern void isr64_stub_31(void);

static idt_entry_t idt[256];

static void idt_set(int vec, void (*isr)(void))
{
    uint64_t a = (uint64_t)(uintptr_t)isr;
    idt[vec].off_lo = (uint16_t)(a & 0xFFFF);
    idt[vec].sel = 0x08; // kernel code selector
    idt[vec].ist = 0;
    idt[vec].type_attr = 0x8E; // present, ring0, interrupt gate
    idt[vec].off_mid = (uint16_t)((a >> 16) & 0xFFFF);
    idt[vec].off_hi = (uint32_t)((a >> 32) & 0xFFFFFFFF);
    idt[vec].zero = 0;
}

void idt64_register_user_int(int vec, void (*isr)(void))
{
    uint64_t a = (uint64_t)(uintptr_t)isr;
    idt[vec].off_lo = (uint16_t)(a & 0xFFFF);
    idt[vec].sel = 0x08;
    idt[vec].ist = 0;
    idt[vec].type_attr = 0xEE; // present, DPL 3, interrupt gate
    idt[vec].off_mid = (uint16_t)((a >> 16) & 0xFFFF);
    idt[vec].off_hi = (uint32_t)((a >> 32) & 0xFFFFFFFF);
    idt[vec].zero = 0;
}

void idt64_init(void)
{
    for (int i = 0; i < 256; i++) {
        idt[i] = (idt_entry_t){0};
    }

    void (*stubs[32])(void) = {
        isr64_stub_0,  isr64_stub_1,  isr64_stub_2,  isr64_stub_3,  isr64_stub_4,  isr64_stub_5,
        isr64_stub_6,  isr64_stub_7,  isr64_stub_8,  isr64_stub_9,  isr64_stub_10, isr64_stub_11,
        isr64_stub_12, isr64_stub_13, isr64_stub_14, isr64_stub_15, isr64_stub_16, isr64_stub_17,
        isr64_stub_18, isr64_stub_19, isr64_stub_20, isr64_stub_21, isr64_stub_22, isr64_stub_23,
        isr64_stub_24, isr64_stub_25, isr64_stub_26, isr64_stub_27, isr64_stub_28, isr64_stub_29,
        isr64_stub_30, isr64_stub_31,
    };

    for (int i = 0; i < 32; i++) {
        idt_set(i, stubs[i]);
    }

    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base = (uint64_t)(uintptr_t)idt;

    __asm__ __volatile__("lidt %0" : : "m"(idtr));
}

int isr64_dispatch(isr_frame_t* f)
{
    if (f->vec == 128) {
        syscall_handler((void*)f);
        return 1;
    }
    if (f->vec == 14) {
        uint64_t cr2 = 0;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        panic_u64("PAGE FAULT cr2=", cr2);
    }
    if (f->vec == 13) {
        panic_u64("GP FAULT rip=", f->rip);
    }
    if (f->vec == 8) {
        panic("DOUBLE FAULT");
    }
    panic_u64("CPU EXCEPTION vec=", f->vec);
}
