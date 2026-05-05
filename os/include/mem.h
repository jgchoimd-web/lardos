#pragma once

#include <stdint.h>

/* Initialize the kernel heap allocator. Safe to call once. */
void mem_init(void);

/* Simple kernel heap allocation API. */
void* kmalloc(uint32_t size);
void kfree(void* ptr);

/* Heap stats for debugging/shell. */
uint32_t mem_heap_start(void);
uint32_t mem_heap_end(void);
uint32_t mem_bytes_free(void);

