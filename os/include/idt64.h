#pragma once

#include <stdint.h>

void idt64_init(void);

/* Register user-callable interrupt (DPL 3). For INT 0x80 syscall. */
void idt64_register_user_int(int vec, void (*isr)(void));

typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t vec;
    uint64_t err;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} isr_frame_t;

/* Return 1 to iret to user (syscall), 0 to halt (exception). */
int isr64_dispatch(isr_frame_t* frame);
