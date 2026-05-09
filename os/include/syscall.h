#pragma once

#include <stdint.h>
#include "ps2.h"

/* Syscall numbers */
#define SYS_WRITE      1
#define SYS_EXIT       2
#define SYS_LDLL_LOAD  3
#define SYS_LDLL_SYM   4
#define SYS_LDLL_CLOSE 5
#define SYS_GUI_PUT_PIXEL  6
#define SYS_GUI_FILL_RECT  7
#define SYS_GUI_DRAW_TEXT 8
#define SYS_GUI_CLEAR     9
#define SYS_GUI_GET_WIDTH  10
#define SYS_GUI_GET_HEIGHT 11
#define SYS_OPEN   12
#define SYS_READ   13
#define SYS_CLOSE  14
#define SYS_GET_TIME  16  /* LardOS Time ticks since 00000-01-01, not Unix seconds */
#define SYS_POLL_KEY  17
#define SYS_GET_KEY   18
#define SYS_LAFILLO_HTML 19
#define SYS_HASH_CRC32  20
#define SYS_HASH_FNV1A  21
#define SYS_BASE64_ENCODE 22
#define SYS_BASE64_DECODE 23
#define SYS_LIPC_SEND     24
#define SYS_LIPC_RECV     25
#define SYS_LIPC_PENDING  26

#define SYSCALL_CAP_FS      0x01u
#define SYSCALL_CAP_LDLL    0x02u
#define SYSCALL_CAP_GUI     0x04u
#define SYSCALL_CAP_KEYS    0x08u
#define SYSCALL_CAP_LIPC    0x10u
#define SYSCALL_CAP_LAFILLO 0x20u
#define SYSCALL_CAP_ALL     (SYSCALL_CAP_FS | SYSCALL_CAP_LDLL | SYSCALL_CAP_GUI | SYSCALL_CAP_KEYS | SYSCALL_CAP_LIPC | SYSCALL_CAP_LAFILLO)
#define SYSCALL_CAP_BASE    0u

/* Register INT 0x80 handler. Must be called after gdt64_init. */
void syscall_init(void);

void syscall_handler(void* frame);

void syscall_append(const char* s, uint32_t len);
const char* syscall_get_output(void);
void syscall_clear_output(void);

/* Push key for user program SYS_GET_KEY. Call from main loop when ps2_kbd_poll returns key. */
void syscall_key_push(ps2_key_t k);

/* Sandbox: when 1, block file/LDLL/network/GUI-write/keys. Allow write/exit/time/screen-size. */
void syscall_set_sandbox(int on);
int syscall_in_sandbox(void);

void syscall_set_caps(uint32_t caps);
uint32_t syscall_get_caps(void);
void syscall_reset_process_state(void);
