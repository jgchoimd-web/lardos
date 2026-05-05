#pragma once

#include <stdint.h>

/* Demo: allocate GC objects, run collection, format stats to out. */
int gc_demo(char* out, uint32_t out_cap);
