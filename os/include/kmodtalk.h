#pragma once

#include <stdint.h>

#define KMODTALK_REPLY_MAX 1024u

void kmodtalk_init(void);
uint32_t kmodtalk_module_count(void);
const char* kmodtalk_module_name(uint32_t index);
const char* kmodtalk_module_help(uint32_t index);
int kmodtalk_send(const char* module, const char* message, char* out, uint32_t out_cap);
int kmodtalk_selftest(void);
