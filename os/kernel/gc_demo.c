/*
 * GC library demo: allocate objects, drop some, run collection, show stats.
 */
#include "gc.h"
#include "string.h"
#include <stdint.h>

int gc_demo(char* out, uint32_t out_cap)
{
    if (!out || out_cap < 24) return -1;
    out[0] = '\0';

    gc_init();

    void* root = gc_alloc(32);
    if (!root) return -1;
    gc_add_root(&root);

    (void)gc_alloc(64);
    (void)gc_alloc(128);

    gc_run();
    uint32_t live = gc_live_count();
    uint32_t bytes = gc_live_bytes();
    gc_remove_root(&root);

    snprintf(out, out_cap, "%u objs, %u bytes", live, bytes);
    return 0;
}
