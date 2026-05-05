#pragma once

#include <stdint.h>

__attribute__((noreturn)) void panic(const char* msg);
__attribute__((noreturn)) void panic_u64(const char* msg, uint64_t v);

