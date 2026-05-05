#include "mem.h"

#include <stdint.h>

typedef struct block_header {
    uint32_t size; /* bytes in payload (not including header) */
    uint32_t next; /* phys/virt address of next header, 0 if none */
    uint32_t free; /* 1 if free */
} block_header_t;

extern uint8_t __kernel_end;

#define HEAP_DEFAULT_SIZE (4u * 1024u * 1024u) /* 4 MiB */
#define ALIGNMENT 16u

static uint32_t g_heap_start = 0;
static uint32_t g_heap_end = 0;
static uint32_t g_free_list = 0; /* address of first block_header_t */
static uint32_t g_initialized = 0;

static inline uint32_t align_up(uint32_t v, uint32_t a)
{
    return (uint32_t)((v + (a - 1)) & ~(a - 1));
}

static inline block_header_t* hdr_from_addr(uint32_t addr)
{
    return (block_header_t*)(uintptr_t)addr;
}

static inline uint32_t addr_from_hdr(block_header_t* h)
{
    return (uint32_t)(uintptr_t)h;
}

static void split_block(block_header_t* h, uint32_t want)
{
    /* Want is payload size (aligned). */
    uint32_t header_sz = (uint32_t)sizeof(block_header_t);
    uint32_t min_split_payload = ALIGNMENT;

    if (h->size < want + header_sz + min_split_payload) {
        return;
    }

    uint32_t new_hdr_addr = addr_from_hdr(h) + header_sz + want;
    new_hdr_addr = align_up(new_hdr_addr, ALIGNMENT);

    /* If alignment pushed too far, skip split. */
    if (new_hdr_addr >= addr_from_hdr(h) + header_sz + h->size) {
        return;
    }

    block_header_t* nh = hdr_from_addr(new_hdr_addr);
    uint32_t old_next = h->next;

    uint32_t h_payload_start = addr_from_hdr(h) + header_sz;
    uint32_t nh_payload_start = new_hdr_addr + header_sz;
    if (nh_payload_start > addr_from_hdr(h) + header_sz + h->size) {
        return;
    }

    h->next = new_hdr_addr;
    nh->next = old_next;
    nh->free = 1;

    /* Remaining payload bytes for the new block. */
    nh->size = (addr_from_hdr(h) + header_sz + h->size) - nh_payload_start;
    h->size = nh_payload_start - h_payload_start - header_sz;
}

static void coalesce(void)
{
    uint32_t header_sz = (uint32_t)sizeof(block_header_t);
    for (block_header_t* h = hdr_from_addr(g_free_list); h; h = h->next ? hdr_from_addr(h->next) : 0) {
        if (!h->free || !h->next) {
            continue;
        }
        block_header_t* n = hdr_from_addr(h->next);
        if (!n->free) {
            continue;
        }

        uint32_t h_end = addr_from_hdr(h) + header_sz + h->size;
        uint32_t n_addr = addr_from_hdr(n);

        if (h_end == n_addr) {
            h->size += header_sz + n->size;
            h->next = n->next;
            /* stay on same h, see if we can merge multiple in a row */
        }
    }
}

void mem_init(void)
{
    if (g_initialized) {
        return;
    }

    uint32_t start = align_up((uint32_t)(uintptr_t)&__kernel_end, ALIGNMENT);
    uint32_t end = start + HEAP_DEFAULT_SIZE;

    g_heap_start = start;
    g_heap_end = end;

    block_header_t* h = hdr_from_addr(start);
    h->size = (end - start) - (uint32_t)sizeof(block_header_t);
    h->next = 0;
    h->free = 1;

    g_free_list = start;
    g_initialized = 1;
}

void* kmalloc(uint32_t size)
{
    if (!g_initialized) {
        mem_init();
    }

    if (size == 0) {
        return 0;
    }

    uint32_t want = align_up(size, ALIGNMENT);
    uint32_t header_sz = (uint32_t)sizeof(block_header_t);

    for (block_header_t* h = hdr_from_addr(g_free_list); h; h = h->next ? hdr_from_addr(h->next) : 0) {
        if (!h->free || h->size < want) {
            continue;
        }

        split_block(h, want);
        h->free = 0;
        return (void*)(uintptr_t)(addr_from_hdr(h) + header_sz);
    }

    return 0;
}

void kfree(void* ptr)
{
    if (!ptr) {
        return;
    }
    if (!g_initialized) {
        return;
    }

    uint32_t header_sz = (uint32_t)sizeof(block_header_t);
    uint32_t p = (uint32_t)(uintptr_t)ptr;

    if (p < g_heap_start + header_sz || p >= g_heap_end) {
        return;
    }

    block_header_t* h = hdr_from_addr(p - header_sz);
    h->free = 1;
    coalesce();
}

uint32_t mem_heap_start(void)
{
    return g_heap_start;
}

uint32_t mem_heap_end(void)
{
    return g_heap_end;
}

uint32_t mem_bytes_free(void)
{
    if (!g_initialized) {
        return 0;
    }

    uint32_t free_bytes = 0;
    for (block_header_t* h = hdr_from_addr(g_free_list); h; h = h->next ? hdr_from_addr(h->next) : 0) {
        if (h->free) {
            free_bytes += h->size;
        }
    }
    return free_bytes;
}

