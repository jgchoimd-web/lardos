/*
 * User mode support: enter user, run sample program.
 */
#include "usermode.h"
#include "mmu.h"
#include "syscall.h"
#include "gdt64.h"
#include <stdint.h>

uint64_t g_usermode_return_rsp;

/* Sample user program: load liblard.ldll, call puts(), exit.
 * Layout: code, then "liblard.ldll\0", "puts\0", "Hello from LDLL!\n\0"
 * Offsets from instruction end: liblard at 69, puts at 82, msg at 87.
 */
static const uint8_t user_prog[] = {
    /* mov eax, 3 (SYS_LDLL_LOAD) */
    0xB8, 0x03, 0x00, 0x00, 0x00,
    /* lea rdi, [rip+0x33] ; "liblard.ldll" at 63, rip at 12 */
    0x48, 0x8D, 0x3D, 0x33, 0x00, 0x00, 0x00,
    /* int 0x80 */
    0xCD, 0x80,
    /* mov ebx, eax ; save handle */
    0x89, 0xC3,
    /* test eax, eax ; handle < 0? */
    0x85, 0xC0,
    /* js +0x22 ; skip to exit if fail */
    0x78, 0x22,
    /* mov eax, 4 (SYS_LDLL_SYM) */
    0xB8, 0x04, 0x00, 0x00, 0x00,
    /* mov edi, ebx */
    0x89, 0xDF,
    /* lea rsi, [rip+0x2a] ; "puts" at 76, rip at 34 */
    0x48, 0x8D, 0x35, 0x2a, 0x00, 0x00, 0x00,
    /* int 0x80 */
    0xCD, 0x80,
    /* lea rdi, [rip+0x26] ; msg at 81, rip at 43 */
    0x48, 0x8D, 0x3D, 0x26, 0x00, 0x00, 0x00,
    /* call rax ; puts(msg) */
    0xFF, 0xD0,
    /* mov eax, 5 (SYS_LDLL_CLOSE) */
    0xB8, 0x05, 0x00, 0x00, 0x00,
    /* mov edi, ebx */
    0x89, 0xDF,
    /* int 0x80 */
    0xCD, 0x80,
    /* mov eax, 2 (SYS_EXIT) */
    0xB8, 0x02, 0x00, 0x00, 0x00,
    /* xor edi, edi */
    0x31, 0xFF,
    /* int 0x80 */
    0xCD, 0x80,
    /* "liblard.ldll\0" */
    'l','i','b','l','a','r','d','.','l','d','l','l',0,
    /* "puts\0" */
    'p','u','t','s',0,
    /* "Hello from LDLL!\n\0" */
    'H','e','l','l','o',' ','f','r','o','m',' ','L','D','L','L','!','\n',0
};

void usermode_init(void)
{
    mmu_map_user_region(USER_ENTRY, USER_STACK);
    uint8_t* dst = (uint8_t*)(uintptr_t)USER_ENTRY;
    for (uint32_t i = 0; i < sizeof(user_prog); i++) {
        dst[i] = user_prog[i];
    }
}

void usermode_run(void)
{
    uint64_t rsp_val;
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp_val));
    g_usermode_return_rsp = rsp_val;

    /* iretq pops: RIP, CS, RFLAGS, RSP, SS. Push in reverse order. */
    uint64_t user_rip = USER_ENTRY;
    uint64_t user_rsp = USER_STACK;
    uint64_t user_cs = GDT64_SEL_UCODE;
    uint64_t user_ss = GDT64_SEL_UDATA;
    uint64_t user_rflags = 0x202;

    __asm__ __volatile__(
        "push %0\n"
        "push %1\n"
        "push %2\n"
        "push %3\n"
        "push %4\n"
        "iretq"
        :
        : "r"(user_ss), "r"(user_rsp), "r"(user_rflags), "r"(user_cs), "r"(user_rip)
        : "memory"
    );
}

#define LARDX_ARGV_MAX  16
#define LARDX_ARGV_BUF  512

void usermode_run_lardx(uint32_t entry, int argc, const char** argv)
{
    uint64_t rsp_val;
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp_val));
    g_usermode_return_rsp = rsp_val;

    uint8_t* stack = (uint8_t*)(uintptr_t)0x007FF000u;
    uint32_t str_off = 64;
    uint64_t* argv_arr = (uint64_t*)(stack + 0);
    int n = argc > LARDX_ARGV_MAX ? LARDX_ARGV_MAX : argc;
    if (n < 0) n = 0;

    for (int i = 0; i < n && str_off < LARDX_ARGV_BUF - 32; i++) {
        const char* s = argv[i] ? argv[i] : "";
        uint32_t pos = str_off;
        argv_arr[i] = 0x007FF000u + pos;
        while (*s && str_off + 1 < LARDX_ARGV_BUF) {
            stack[str_off++] = (uint8_t)*s++;
        }
        stack[str_off++] = '\0';
    }
    argv_arr[n] = 0;

    uint64_t user_rsp = 0x007FF000u;
    uint64_t user_rip = entry;
    uint64_t user_cs = GDT64_SEL_UCODE;
    uint64_t user_ss = GDT64_SEL_UDATA;
    uint64_t user_rflags = 0x202;

    __asm__ __volatile__(
        "movq %0, %0\n"
        "movq %1, %1\n"
        "pushq %2\n"
        "pushq %3\n"
        "pushq %4\n"
        "pushq %5\n"
        "pushq %6\n"
        "iretq"
        :
        : "D"((uint64_t)n), "S"((uint64_t)0x007FF000u),
          "r"(user_ss), "r"(user_rsp), "r"(user_rflags), "r"(user_cs), "r"(user_rip)
        : "memory"
    );
}
