#pragma once

#include <stdint.h>

void panic_runtime_ready(void);
__attribute__((noreturn)) void panic(const char* msg);
__attribute__((noreturn)) void panic_u64(const char* msg, uint64_t v);
