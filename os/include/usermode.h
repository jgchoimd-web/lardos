#pragma once

#include <stdint.h>

/* Map user region and prepare embedded sample program.
 * Must be called after mmu_init_protection. */
void usermode_init(void);

/* Run user program at USER_ENTRY. Does not return if program runs;
 * returns when user calls SYS_EXIT. */
void usermode_run(void);

/* Run LARDX user program. entry=entry VA, argc/argv for command line.
 * Builds argv on stack and iretq to user. Does not return until SYS_EXIT. */
void usermode_run_lardx(uint32_t entry, int argc, const char** argv);

/* User virtual addresses (identity-mapped for simplicity) */
#define USER_ENTRY   0x00400000
#define USER_STACK   0x007FF000   /* stack top, grows down */
