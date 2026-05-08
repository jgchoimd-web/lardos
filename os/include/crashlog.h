#pragma once

#include <stdint.h>

void crashlog_init(void);
void crashlog_record(const char* kind, const char* message);
void crashlog_record_u64(const char* kind, const char* message, uint64_t value);
void crashlog_record_panic(const char* message);
void crashlog_record_panic_u64(const char* message, uint64_t value);
int crashlog_clear(void);
const char* crashlog_text(void);
uint32_t crashlog_count(void);
int crashlog_selftest(void);
