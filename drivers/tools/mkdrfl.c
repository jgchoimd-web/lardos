/*
 * mkdrfl - write a DRFL 1 descriptor.
 *
 * Usage:
 *   mkdrfl out.drfl vendor_hex device_hex net|block driver-name
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned parse_hex16(const char* s)
{
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (!s[0] || (end && *end) || v > 0xFFFFul) {
        fprintf(stderr, "mkdrfl: bad hex16 value: %s\n", s);
        exit(2);
    }
    return (unsigned)v;
}

static unsigned parse_type(const char* s)
{
    if (strcmp(s, "net") == 0) return 0;
    if (strcmp(s, "block") == 0) return 1;
    fprintf(stderr, "mkdrfl: type must be net or block\n");
    exit(2);
}

static void put16(FILE* f, unsigned v)
{
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
}

int main(int argc, char** argv)
{
    FILE* f;
    unsigned vendor;
    unsigned device;
    unsigned type;
    size_t name_len;

    if (argc != 6) {
        fprintf(stderr, "usage: mkdrfl out.drfl vendor_hex device_hex net|block driver-name\n");
        return 2;
    }

    vendor = parse_hex16(argv[2]);
    device = parse_hex16(argv[3]);
    type = parse_type(argv[4]);
    name_len = strlen(argv[5]);
    if (name_len == 0 || name_len > 31) {
        fprintf(stderr, "mkdrfl: driver name must be 1..31 bytes\n");
        return 2;
    }

    f = fopen(argv[1], "wb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }

    fputc('D', f);
    fputc('R', f);
    fputc('F', f);
    fputc('L', f);
    fputc(1, f);
    fputc(0, f);
    fputc(0, f);
    fputc(0, f);
    put16(f, 1);
    put16(f, vendor);
    put16(f, device);
    fputc((int)type, f);
    fputc((int)name_len, f);
    fwrite(argv[5], 1, name_len, f);
    fclose(f);
    return 0;
}
