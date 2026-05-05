#pragma once

#include <stdint.h>

/*
 * Garbage collection library for the kernel heap.
 * Mark-and-sweep, conservative scanning (no object layout needed).
 *
 * Usage:
 *   gc_init();                    // after mem_init()
 *   void* p = gc_alloc(64);
 *   gc_add_root(&p);              // p is a root; GC won't collect it
 *   gc_run();                      // collect when needed
 *   gc_remove_root(&p);
 */

/* Initialize GC. Call after mem_init(). */
void gc_init(void);

/* Allocate GC-managed memory. Returns 0 on failure. Do not kfree. */
void* gc_alloc(uint32_t size);

/* Register/remove root. root_addr = address of a pointer variable. */
void gc_add_root(void** root_addr);
void gc_remove_root(void** root_addr);

/* Run collection. Frees unreachable objects. */
void gc_run(void);

/* Stats */
uint32_t gc_live_count(void);
uint32_t gc_live_bytes(void);
