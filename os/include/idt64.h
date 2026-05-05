#pragma once

void idt64_init(void);

/* Register user-callable interrupt (DPL 3). For INT 0x80 syscall. */
void idt64_register_user_int(int vec, void (*isr)(void));

/* Return 1 to iret to user (syscall), 0 to halt (exception). */
int isr64_dispatch(void* frame);

