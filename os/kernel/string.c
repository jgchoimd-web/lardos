#include "string.h"
#include <stdint.h>

void* memcpy(void* d, const void* s, size_t n)
{
    unsigned char* a = (unsigned char*)d;
    const unsigned char* b = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) a[i] = b[i];
    return d;
}

void* memmove(void* d, const void* s, size_t n)
{
    unsigned char* a = (unsigned char*)d;
    const unsigned char* b = (const unsigned char*)s;
    if ((uintptr_t)a <= (uintptr_t)b || (uintptr_t)a >= (uintptr_t)b + n) {
        for (size_t i = 0; i < n; i++) a[i] = b[i];
    } else {
        for (size_t i = n; i > 0; i--) a[i - 1] = b[i - 1];
    }
    return d;
}

void* memset(void* s, int c, size_t n)
{
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

int memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char* s)
{
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b)
{
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n)
{
    if (n == 0) return 0;
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
    return 0;
}
