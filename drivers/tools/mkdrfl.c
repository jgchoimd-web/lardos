/*
 * mkdrfl - write a DRFL 2 source-carrying driver file.
 *
 * Usage:
 *   mkdrfl out.drfl vendor_hex device_hex net|block driver-name
 *
 * The generated file is text so the user can edit the driver body directly.
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

static const char* parse_type(const char* s)
{
    if (strcmp(s, "net") == 0) return "net";
    if (strcmp(s, "block") == 0) return "block";
    fprintf(stderr, "mkdrfl: type must be net or block\n");
    exit(2);
}

int main(int argc, char** argv)
{
    FILE* f;
    unsigned vendor;
    unsigned device;
    const char* type;
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

    fprintf(f, "DRFL 2\n");
    fprintf(f, "ID %s\n", argv[5]);
    fprintf(f, "TYPE %s\n", type);
    fprintf(f, "PCI %04X %04X\n", vendor, device);
    fprintf(f, "LANG DRFL-C\n");
    fprintf(f, "CODE int drfl_init(void* ctx) {\n");
    fprintf(f, "CODE   /* user-owned driver body goes here */\n");
    fprintf(f, "CODE   (void)ctx;\n");
    fprintf(f, "CODE   return 0;\n");
    fprintf(f, "CODE }\n");
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
