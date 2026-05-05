#include <stdarg.h>
#include <stddef.h>

int vsnprintf_(char* buffer, size_t count, const char* format, va_list va);
int snprintf_(char* buffer, size_t count, const char* format, ...);

int vsnprintf(char* s, size_t n, const char* fmt, va_list ap)
{
    return vsnprintf_(s, n, fmt, ap);
}

int snprintf(char* s, size_t n, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf_(s, n, fmt, ap);
    va_end(ap);
    return r;
}
