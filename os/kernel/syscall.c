/*
 * INT 0x80 syscall handler.
 * rax = syscall number, rdi/rsi/rdx = args.
 */
#include "syscall.h"
#include "lipc.h"
#include "idt64.h"
#include "gdt64.h"
#include "ldll.h"
#include "gui.h"
#include "fs.h"
#include "rtc.h"
#include "lardtime.h"
#include "ps2.h"
#include "lafillo.h"
#include "hash.h"
#include "base64.h"
#include "rxr.h"
#include "usermode.h"
#include <stdint.h>

#define FD_MAX  8
#define FD_BASE 2
#define SYS_PATH_MAX 64

static int is_user_ptr(uintptr_t p, uint32_t max_len)
{
    if (p < USER_VALID_LO || p >= USER_VALID_HI) return 0;
    if (p + max_len < p || p + max_len >= USER_VALID_HI) return 0;
    return 1;
}

static int copy_user_string(const char* user_src, char* dst, uint32_t cap)
{
    if (!is_user_ptr((uintptr_t)user_src, cap)) return -1;
    for (uint32_t i = 0; i < cap - 1; i++) {
        char c = user_src[i];
        dst[i] = c;
        if (c == '\0') return (int)i;
    }
    dst[cap - 1] = '\0';
    return -1;
}

typedef struct {
    const FsFile* f;
    FsWritableFile* w;
    uint32_t offset;
} fd_entry_t;

static fd_entry_t s_fds[FD_MAX];

#define KEY_QUEUE_SIZE 32
static ps2_key_t s_key_queue[KEY_QUEUE_SIZE];
static unsigned int s_key_head;
static unsigned int s_key_tail;

void syscall_key_push(ps2_key_t k)
{
    unsigned int next = (s_key_tail + 1) % KEY_QUEUE_SIZE;
    if (next != s_key_head) {
        s_key_queue[s_key_tail] = k;
        s_key_tail = next;
    }
}

/* syscall_output appends to this; gui reads it for User tab. */
static char g_syscall_out[512];
static uint32_t g_syscall_out_len;

static uint32_t s_caps = SYSCALL_CAP_ALL;

void syscall_set_sandbox(int on)
{
    s_caps = on ? SYSCALL_CAP_BASE : SYSCALL_CAP_ALL;
}

int syscall_in_sandbox(void)
{
    return s_caps != SYSCALL_CAP_ALL;
}

void syscall_set_caps(uint32_t caps)
{
    s_caps = caps & SYSCALL_CAP_ALL;
}

uint32_t syscall_get_caps(void)
{
    return s_caps;
}

void syscall_reset_process_state(void)
{
    for (int i = 0; i < FD_MAX; i++) {
        s_fds[i].f = 0;
        s_fds[i].w = 0;
        s_fds[i].offset = 0;
    }
    s_key_head = 0;
    s_key_tail = 0;
}

void syscall_append(const char* s, uint32_t len)
{
    uint32_t rem = sizeof(g_syscall_out) - 1 - g_syscall_out_len;
    if (len > rem) len = rem;
    for (uint32_t i = 0; i < len && s[i]; i++) {
        g_syscall_out[g_syscall_out_len++] = s[i];
    }
    g_syscall_out[g_syscall_out_len] = '\0';
}

const char* syscall_get_output(void)
{
    return g_syscall_out;
}

void syscall_clear_output(void)
{
    g_syscall_out_len = 0;
    g_syscall_out[0] = '\0';
}

extern void syscall_exit_to_kernel(void);

#define SANDBOX_DENY (-1)

static int cap_blocks(uint64_t nr)
{
    if (s_caps == SYSCALL_CAP_ALL) return 0;
    switch ((uint32_t)nr) {
    case SYS_WRITE:
    case SYS_EXIT:
    case SYS_GUI_GET_WIDTH:
    case SYS_GUI_GET_HEIGHT:
    case SYS_GET_TIME:
    case SYS_HASH_CRC32:
    case SYS_HASH_FNV1A:
    case SYS_BASE64_ENCODE:
    case SYS_BASE64_DECODE:
    case SYS_CLOSE:
        return 0;
    case SYS_OPEN:
    case SYS_READ:
        return (s_caps & SYSCALL_CAP_FS) ? 0 : 1;
    case SYS_LDLL_LOAD:
    case SYS_LDLL_SYM:
    case SYS_LDLL_CLOSE:
        return (s_caps & SYSCALL_CAP_LDLL) ? 0 : 1;
    case SYS_GUI_PUT_PIXEL:
    case SYS_GUI_FILL_RECT:
    case SYS_GUI_DRAW_TEXT:
    case SYS_GUI_CLEAR:
        return (s_caps & SYSCALL_CAP_GUI) ? 0 : 1;
    case SYS_POLL_KEY:
    case SYS_GET_KEY:
        return (s_caps & SYSCALL_CAP_KEYS) ? 0 : 1;
    case SYS_LAFILLO_HTML:
        return (s_caps & SYSCALL_CAP_LAFILLO) ? 0 : 1;
    case SYS_LIPC_SEND:
    case SYS_LIPC_RECV:
    case SYS_LIPC_PENDING:
        return (s_caps & SYSCALL_CAP_LIPC) ? 0 : 1;
    default:
        return 1;
    }
}

void syscall_handler(void* frame)
{
    isr_frame_t* f = (isr_frame_t*)frame;
    uint64_t nr = f->rax;
    uint64_t a1 = f->rdi;
    uint64_t a2 = f->rsi;
    uint64_t a3 = f->rdx;

    if (cap_blocks(nr)) {
        f->rax = (uint64_t)(int64_t)SANDBOX_DENY;
        return;
    }

    if (nr == SYS_WRITE) {
        if (a1 == 1 && a2 != 0 && a3 > 0) {
            const char* buf = (const char*)(uintptr_t)a2;
            uint32_t len = (uint32_t)a3;
            if (len > 400) len = 400;
            syscall_append(buf, len);
        }
        return;
    }

    if (nr == SYS_EXIT) {
        syscall_exit_to_kernel();
        __builtin_unreachable();
    }

    if (nr == SYS_LDLL_LOAD) {
        f->rax = (uint64_t)(int64_t)ldll_load((const char*)(uintptr_t)a1);
        return;
    }
    if (nr == SYS_LDLL_SYM) {
        f->rax = (uint64_t)(uintptr_t)ldll_sym((int)(int64_t)a1, (const char*)(uintptr_t)a2);
        return;
    }
    if (nr == SYS_LDLL_CLOSE) {
        ldll_close((int)(int64_t)a1);
        return;
    }

    uint64_t a4 = f->r10;
    uint64_t a5 = f->r8;

    if (nr == SYS_GUI_PUT_PIXEL) {
        gui_syscall_put_pixel((uint16_t)a1, (uint16_t)a2, (uint32_t)a3);
        return;
    }
    if (nr == SYS_GUI_FILL_RECT) {
        gui_syscall_fill_rect((uint16_t)a1, (uint16_t)a2, (uint16_t)a3, (uint16_t)a4, (uint32_t)a5);
        return;
    }
    if (nr == SYS_GUI_DRAW_TEXT) {
        if (is_user_ptr(a3, 256)) {
            gui_syscall_draw_text((uint16_t)a1, (uint16_t)a2, (const char*)(uintptr_t)a3, (uint32_t)a4, (uint32_t)a5);
        }
        return;
    }
    if (nr == SYS_GUI_CLEAR) {
        gui_syscall_clear((uint32_t)a1);
        return;
    }
    if (nr == SYS_GUI_GET_WIDTH) {
        f->rax = gui_syscall_get_width();
        return;
    }
    if (nr == SYS_GUI_GET_HEIGHT) {
        f->rax = gui_syscall_get_height();
        return;
    }

    if (nr == SYS_OPEN) {
        if (!is_user_ptr(a1, SYS_PATH_MAX)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        char path[SYS_PATH_MAX];
        if (copy_user_string((const char*)(uintptr_t)a1, path, SYS_PATH_MAX) < 0) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        {
            char resolved[SYS_PATH_MAX];
            if (rxr_resolve_path(path, resolved, sizeof(resolved)) >= 0) {
                for (uint32_t pi = 0; pi < sizeof(path); pi++) {
                    path[pi] = resolved[pi];
                    if (resolved[pi] == '\0') break;
                }
                path[sizeof(path) - 1u] = '\0';
            }
        }
        for (int i = 0; i < FD_MAX; i++) {
            if (s_fds[i].f || s_fds[i].w) continue;
            const FsFile* rf = fs_open(path);
            if (rf) {
                s_fds[i].f = rf;
                s_fds[i].w = 0;
                s_fds[i].offset = 0;
                f->rax = (uint64_t)(FD_BASE + i);
                return;
            }
            FsWritableFile* wf = fs_open_writable(path);
            if (wf) {
                s_fds[i].f = 0;
                s_fds[i].w = wf;
                s_fds[i].offset = 0;
                f->rax = (uint64_t)(FD_BASE + i);
                return;
            }
            break;
        }
        f->rax = (uint64_t)(int64_t)-1;
        return;
    }

    if (nr == SYS_READ) {
        int fd = (int)(int64_t)a1;
        int idx = fd - FD_BASE;
        if (idx < 0 || idx >= FD_MAX || (!s_fds[idx].f && !s_fds[idx].w)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        uint32_t len = (uint32_t)a3;
        if (len > 4096) len = 4096;
        if (!is_user_ptr(a2, len)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        uint32_t n = 0;
        if (s_fds[idx].f) {
            n = fs_read(s_fds[idx].f, s_fds[idx].offset, (uint8_t*)(uintptr_t)a2, len);
            s_fds[idx].offset += n;
        } else if (s_fds[idx].w) {
            uint32_t sz = s_fds[idx].w->size;
            if (s_fds[idx].offset < sz) {
                uint32_t rem = sz - s_fds[idx].offset;
                if (len > rem) len = rem;
                for (uint32_t j = 0; j < len; j++)
                    ((uint8_t*)(uintptr_t)a2)[j] = s_fds[idx].w->data[s_fds[idx].offset + j];
                n = len;
                s_fds[idx].offset += n;
            }
        }
        f->rax = (uint64_t)n;
        return;
    }

    if (nr == SYS_CLOSE) {
        int fd = (int)(int64_t)a1;
        int idx = fd - FD_BASE;
        if (idx >= 0 && idx < FD_MAX) {
            s_fds[idx].f = 0;
            s_fds[idx].w = 0;
        }
        return;
    }

    if (nr == SYS_GET_TIME) {
        int64_t t = lardtime_now_ticks();
        f->rax = (uint64_t)(int64_t)t;
        return;
    }

    if (nr == SYS_POLL_KEY) {
        f->rax = (s_key_head != s_key_tail) ? 1 : 0;
        return;
    }

    if (nr == SYS_GET_KEY) {
        if (s_key_head == s_key_tail) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        if (!is_user_ptr(a1, 8)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        ps2_key_t k = s_key_queue[s_key_head];
        s_key_head = (s_key_head + 1) % KEY_QUEUE_SIZE;
        *(uint32_t*)(uintptr_t)a1 = (uint32_t)k.kind;
        *((uint32_t*)(uintptr_t)a1 + 1) = (uint32_t)(unsigned char)k.ch;
        f->rax = 0;
        return;
    }

    if (nr == SYS_LAFILLO_HTML) {
        uint32_t in_len = (uint32_t)a2;
        uint32_t out_cap = (uint32_t)a4;
        if (in_len > 4096) in_len = 4096;
        if (out_cap > 4096) out_cap = 4096;
        if (!is_user_ptr(a1, in_len) || !is_user_ptr(a3, out_cap)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        static char kern_in[4096];
        static char kern_out[4096];
        for (uint32_t i = 0; i < in_len; i++) kern_in[i] = ((const char*)(uintptr_t)a1)[i];
        kern_in[in_len] = '\0';
        if (lafillo_http_to_text(kern_in, in_len, kern_out, sizeof(kern_out)) != 0) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        uint32_t n = 0;
        while (kern_out[n] && n < out_cap - 1) {
            ((char*)(uintptr_t)a3)[n] = kern_out[n];
            n++;
        }
        ((char*)(uintptr_t)a3)[n] = '\0';
        f->rax = (uint64_t)n;
        return;
    }

    if (nr == SYS_HASH_CRC32) {
        uint32_t len = (uint32_t)a2;
        if (len > 4096) len = 4096;
        if (!a1 || !is_user_ptr(a1, len)) {
            f->rax = 0;
            return;
        }
        f->rax = hash_crc32((const uint8_t*)(uintptr_t)a1, len);
        return;
    }

    if (nr == SYS_HASH_FNV1A) {
        uint32_t len = (uint32_t)a2;
        if (len > 4096) len = 4096;
        if (!a1 || !is_user_ptr(a1, len)) {
            f->rax = 0;
            return;
        }
        f->rax = hash_fnv1a((const uint8_t*)(uintptr_t)a1, len);
        return;
    }

    if (nr == SYS_BASE64_ENCODE) {
        uint32_t in_len = (uint32_t)a2;
        uint32_t out_cap = (uint32_t)a4;
        if (in_len > 3072) in_len = 3072;
        if (out_cap > 4096) out_cap = 4096;
        if (!is_user_ptr(a1, in_len) || !is_user_ptr(a3, out_cap)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        uint32_t n = base64_encode((const uint8_t*)(uintptr_t)a1, in_len, (char*)(uintptr_t)a3);
        if (n >= out_cap) { f->rax = (uint64_t)(int64_t)-1; return; }
        ((char*)(uintptr_t)a3)[n] = '\0';
        f->rax = (uint64_t)n;
        return;
    }

    if (nr == SYS_BASE64_DECODE) {
        uint32_t in_len = (uint32_t)a2;
        if (in_len > 4096) in_len = 4096;
        if (!is_user_ptr(a1, in_len) || !is_user_ptr(a3, in_len)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        uint32_t n = base64_decode((const char*)(uintptr_t)a1, in_len, (uint8_t*)(uintptr_t)a3);
        f->rax = (uint64_t)(int64_t)(int32_t)n;
        return;
    }

    if (nr == SYS_LIPC_SEND) {
        uint32_t port = (uint32_t)a1;
        uint32_t len = (uint32_t)a3;
        if (len > LIPC_MAX_PAYLOAD) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        if (!len || !is_user_ptr(a2, len)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        int r = lipc_send(port, (const void*)(uintptr_t)a2, len);
        f->rax = (uint64_t)(int64_t)r;
        return;
    }

    if (nr == SYS_LIPC_RECV) {
        uint32_t port = (uint32_t)a1;
        uint32_t cap = (uint32_t)a3;
        if (cap > 4096) {
            cap = 4096;
        }
        if (!cap || !is_user_ptr(a2, cap)) {
            f->rax = (uint64_t)(int64_t)-1;
            return;
        }
        int r = lipc_recv(port, (void*)(uintptr_t)a2, cap);
        f->rax = (uint64_t)(int64_t)r;
        return;
    }

    if (nr == SYS_LIPC_PENDING) {
        uint32_t port = (uint32_t)a1;
        f->rax = (uint64_t)lipc_pending(port);
        return;
    }
}

void syscall_init(void)
{
    lipc_init();
    extern void isr64_stub_128(void);
    idt64_register_user_int(0x80, isr64_stub_128);
}
