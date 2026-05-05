/*
 * Garbage collection: mark-and-sweep with conservative scanning.
 * Uses kmalloc/kfree; objects have GC header before payload.
 */
#include "gc.h"
#include "mem.h"
#include "console.h"

#include <stdint.h>

typedef struct gc_block {
    struct gc_block* next;
    uint32_t size;
    uint8_t marked;
    uint8_t _pad[3];
} gc_block_t;

#define GC_HDR ((uint32_t)sizeof(gc_block_t))

static gc_block_t* g_head;
static void** g_roots[64];
static int g_nroots;
static int g_inited;
static uint32_t g_live_count;
static uint32_t g_live_bytes;

static inline uintptr_t block_payload(gc_block_t* b)
{
    return (uintptr_t)b + GC_HDR;
}

static int is_heap_ptr(uintptr_t v)
{
    uint32_t start = mem_heap_start();
    uint32_t end = mem_heap_end();
    uint32_t va = (uint32_t)(v & 0xFFFFFFFFu);
    return va >= start && va < end;
}

static gc_block_t* find_block(void* payload)
{
    uintptr_t p = (uintptr_t)payload;
    for (gc_block_t* b = g_head; b; b = b->next) {
        uintptr_t beg = block_payload(b);
        if (p >= beg && p < beg + b->size)
            return b;
    }
    return 0;
}

static void mark(gc_block_t* b)
{
    if (!b || b->marked) return;
    b->marked = 1;

    /* Conservative scan: every aligned word in payload might be a pointer. */
    uint8_t* mem = (uint8_t*)(uintptr_t)block_payload(b);
    uint32_t n = b->size;
    uint32_t step = (uint32_t)sizeof(uintptr_t);

    for (uint32_t i = 0; i + step <= n; i += step) {
        uintptr_t word = (step == 4) ? (uintptr_t)*(uint32_t*)(mem + i) : *(uintptr_t*)(mem + i);
        if (is_heap_ptr(word)) {
            gc_block_t* other = find_block((void*)word);
            if (other) mark(other);
        }
    }
}

static void mark_phase(void)
{
    for (gc_block_t* b = g_head; b; b = b->next)
        b->marked = 0;

    for (int i = 0; i < g_nroots; i++) {
        void* p = g_roots[i] ? *g_roots[i] : 0;
        if (p) {
            gc_block_t* b = find_block(p);
            if (b) mark(b);
        }
    }
}

static void sweep_phase(void)
{
    gc_block_t** prev = &g_head;
    gc_block_t* b = g_head;
    g_live_count = 0;
    g_live_bytes = 0;

    while (b) {
        if (!b->marked) {
            *prev = b->next;
            void* to_free = (void*)(uintptr_t)b;
            b = b->next;
            kfree(to_free);
            continue;
        }
        g_live_count++;
        g_live_bytes += b->size;
        prev = &b->next;
        b = b->next;
    }
}

void gc_init(void)
{
    if (g_inited) return;
    g_head = 0;
    g_nroots = 0;
    g_live_count = 0;
    g_live_bytes = 0;
    g_inited = 1;
}

void* gc_alloc(uint32_t size)
{
    if (!g_inited) gc_init();
    if (size == 0) return 0;

    uint32_t total = GC_HDR + size;
    gc_block_t* b = (gc_block_t*)kmalloc(total);
    if (!b) return 0;

    b->next = g_head;
    b->size = size;
    b->marked = 0;
    g_head = b;

    return (void*)block_payload(b);
}

void gc_add_root(void** root_addr)
{
    if (!root_addr || g_nroots >= 64) return;
    for (int i = 0; i < g_nroots; i++)
        if (g_roots[i] == root_addr) return;
    g_roots[g_nroots++] = root_addr;
}

void gc_remove_root(void** root_addr)
{
    for (int i = 0; i < g_nroots; i++) {
        if (g_roots[i] == root_addr) {
            g_roots[i] = g_roots[--g_nroots];
            return;
        }
    }
}

void gc_run(void)
{
    if (!g_inited || !g_head) return;
    mark_phase();
    sweep_phase();
}

uint32_t gc_live_count(void)
{
    return g_live_count;
}

uint32_t gc_live_bytes(void)
{
    return g_live_bytes;
}
