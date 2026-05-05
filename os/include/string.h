#pragma once

#include <stddef.h>
#include <stdarg.h>

void* memcpy(void* d, const void* s, size_t n);
void* memmove(void* d, const void* s, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int snprintf(char* s, size_t n, const char* fmt, ...);
int vsnprintf(char* s, size_t n, const char* fmt, va_list ap);
