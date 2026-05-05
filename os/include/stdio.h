#pragma once

#include <stddef.h>
#include <stdarg.h>

int snprintf(char* s, size_t n, const char* fmt, ...);
int vsnprintf(char* s, size_t n, const char* fmt, va_list ap);
