#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char* buf;
    size_t cap;
    size_t len;
} fmt_out_t;

static void out_ch(fmt_out_t* out, char ch)
{
    if (out->cap > 0 && out->len + 1 < out->cap) {
        out->buf[out->len] = ch;
    }
    out->len++;
}

static void out_repeat(fmt_out_t* out, char ch, int count)
{
    while (count-- > 0) {
        out_ch(out, ch);
    }
}

static void out_strn(fmt_out_t* out, const char* s, int n)
{
    while (n-- > 0 && *s) {
        out_ch(out, *s++);
    }
}

static int utoa_rev(uint64_t value, unsigned base, int uppercase, char* tmp)
{
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (value == 0) {
        tmp[n++] = '0';
        return n;
    }
    while (value != 0) {
        tmp[n++] = digits[value % base];
        value /= base;
    }
    return n;
}

static void out_unsigned(fmt_out_t* out, uint64_t value, unsigned base, int uppercase,
                         int width, int zero_pad, int left_align,
                         const char* prefix)
{
    char tmp[32];
    int digits = utoa_rev(value, base, uppercase, tmp);
    int prefix_len = 0;
    while (prefix && prefix[prefix_len]) {
        prefix_len++;
    }
    int total = digits + prefix_len;
    int pad = width > total ? width - total : 0;
    if (!left_align && !zero_pad) out_repeat(out, ' ', pad);
    out_strn(out, prefix ? prefix : "", prefix_len);
    if (!left_align && zero_pad) out_repeat(out, '0', pad);
    while (digits-- > 0) {
        out_ch(out, tmp[digits]);
    }
    if (left_align) out_repeat(out, ' ', pad);
}

static void out_signed(fmt_out_t* out, int64_t value, int width, int zero_pad, int left_align)
{
    uint64_t mag;
    int neg = value < 0;
    if (neg) {
        mag = (uint64_t)(-(value + 1)) + 1u;
    } else {
        mag = (uint64_t)value;
    }

    char tmp[32];
    int digits = utoa_rev(mag, 10, 0, tmp);
    int total = digits + neg;
    int pad = width > total ? width - total : 0;
    if (!left_align && !zero_pad) out_repeat(out, ' ', pad);
    if (neg) out_ch(out, '-');
    if (!left_align && zero_pad) out_repeat(out, '0', pad);
    while (digits-- > 0) {
        out_ch(out, tmp[digits]);
    }
    if (left_align) out_repeat(out, ' ', pad);
}

int vsnprintf_(char* buffer, size_t count, const char* format, va_list va)
{
    fmt_out_t out = { buffer, count, 0 };
    const char* p = format ? format : "";

    while (*p) {
        if (*p != '%') {
            out_ch(&out, *p++);
            continue;
        }
        p++;
        if (*p == '%') {
            out_ch(&out, *p++);
            continue;
        }

        int left_align = 0;
        int zero_pad = 0;
        if (*p == '-') {
            left_align = 1;
            p++;
        }
        if (*p == '0') {
            zero_pad = 1;
            p++;
        }

        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int length = 0;
        if (*p == 'l') {
            length = 1;
            p++;
            if (*p == 'l') {
                length = 2;
                p++;
            }
        } else if (*p == 'z') {
            length = 3;
            p++;
        }

        char spec = *p ? *p++ : '\0';
        switch (spec) {
        case 'c': {
            char ch = (char)va_arg(va, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!left_align) out_repeat(&out, ' ', pad);
            out_ch(&out, ch);
            if (left_align) out_repeat(&out, ' ', pad);
            break;
        }
        case 's': {
            const char* s = va_arg(va, const char*);
            if (!s) s = "(null)";
            int n = 0;
            while (s[n]) n++;
            int pad = width > n ? width - n : 0;
            if (!left_align) out_repeat(&out, ' ', pad);
            out_strn(&out, s, n);
            if (left_align) out_repeat(&out, ' ', pad);
            break;
        }
        case 'd':
        case 'i':
            if (length == 2) out_signed(&out, va_arg(va, long long), width, zero_pad, left_align);
            else if (length == 1) out_signed(&out, va_arg(va, long), width, zero_pad, left_align);
            else out_signed(&out, va_arg(va, int), width, zero_pad, left_align);
            break;
        case 'u':
            if (length == 3) out_unsigned(&out, va_arg(va, size_t), 10, 0, width, zero_pad, left_align, 0);
            else if (length == 2) out_unsigned(&out, va_arg(va, unsigned long long), 10, 0, width, zero_pad, left_align, 0);
            else if (length == 1) out_unsigned(&out, va_arg(va, unsigned long), 10, 0, width, zero_pad, left_align, 0);
            else out_unsigned(&out, va_arg(va, unsigned int), 10, 0, width, zero_pad, left_align, 0);
            break;
        case 'x':
        case 'X':
            if (length == 3) out_unsigned(&out, va_arg(va, size_t), 16, spec == 'X', width, zero_pad, left_align, 0);
            else if (length == 2) out_unsigned(&out, va_arg(va, unsigned long long), 16, spec == 'X', width, zero_pad, left_align, 0);
            else if (length == 1) out_unsigned(&out, va_arg(va, unsigned long), 16, spec == 'X', width, zero_pad, left_align, 0);
            else out_unsigned(&out, va_arg(va, unsigned int), 16, spec == 'X', width, zero_pad, left_align, 0);
            break;
        case 'p':
            out_unsigned(&out, (uintptr_t)va_arg(va, void*), 16, 0, width, zero_pad, left_align, "0x");
            break;
        case '\0':
            out_ch(&out, '%');
            break;
        default:
            out_ch(&out, '%');
            out_ch(&out, spec);
            break;
        }
    }

    if (count > 0) {
        size_t pos = out.len < count ? out.len : count - 1;
        buffer[pos] = '\0';
    }
    return (int)out.len;
}

int snprintf_(char* buffer, size_t count, const char* format, ...)
{
    va_list va;
    va_start(va, format);
    int r = vsnprintf_(buffer, count, format, va);
    va_end(va);
    return r;
}
