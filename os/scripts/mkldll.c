/*
 * mkldll - Create LDLL (LardOS Dynamic Link Library) files.
 * Replaces mkldll.py. No Python required.
 *
 * Usage: mkldll lard|gui|lafillo|ldll -o out.ldll [--embed embed.inc]
 *
 * When adding a new LDLL: (1) add lib*_code[] and exports here,
 * (2) add the name to LDLL_LIBS in deps.mk.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAGIC "LDLL"
#define VERSION 1

typedef struct {
    const char* name;
    uint32_t offset;
} export_t;

static void write_u16(unsigned char* p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void write_u32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static size_t build_ldll(unsigned char* out, size_t cap, const unsigned char* code, size_t code_sz,
                         const export_t* exports, size_t export_count) {
    size_t pos = 0;
    if (pos + 4 <= cap) memcpy(out + pos, MAGIC, 4);
    pos += 4;
    if (pos + 4 <= cap) {
        out[pos++] = VERSION;
        out[pos++] = 0;
        out[pos++] = 0;
        out[pos++] = 0;
    }
    if (pos + 8 <= cap) {
        write_u32(out + pos, (uint32_t)code_sz);
        write_u32(out + pos + 4, 0);
        pos += 8;
    }
    if (pos + code_sz <= cap) memcpy(out + pos, code, code_sz);
    pos += code_sz;
    if (pos + 2 <= cap) write_u16(out + pos, (uint16_t)export_count);
    pos += 2;

    for (size_t i = 0; i < export_count; i++) {
        size_t nlen = strlen(exports[i].name);
        if (pos + 1 + nlen + 4 <= cap) {
            out[pos++] = (unsigned char)nlen;
            memcpy(out + pos, exports[i].name, nlen);
            pos += nlen;
            write_u32(out + pos, exports[i].offset);
            pos += 4;
        }
    }
    return pos;
}

/* liblard: puts, strlen, putc, memcpy, memset, atoi */
static const unsigned char liblard_code[] = {
    /* puts: 0 */
    0x53, 0x48, 0x89, 0xFB, 0x31, 0xC9, 0x0F, 0xB6, 0x04, 0x0B, 0x84, 0xC0, 0x74, 0x0A,
    0xFF, 0xC1, 0xEB, 0xF4, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xBF, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x89, 0xDE, 0x89, 0xCA, 0xCD, 0x80, 0x5B, 0xC3,
    /* strlen: 37 */
    0x31, 0xC0, 0x80, 0x3C, 0x07, 0x00, 0x74, 0x04, 0xFF, 0xC0, 0xEB, 0xF6, 0xC3,
    /* putc: 50 */
    0x48, 0x83, 0xEC, 0x10, 0x88, 0x3C, 0x24, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xBF, 0x01,
    0x00, 0x00, 0x00, 0x48, 0x89, 0xE6, 0xBA, 0x01, 0x00, 0x00, 0x00, 0xCD, 0x80, 0x48,
    0x83, 0xC4, 0x10, 0xC3,
    /* memcpy: 82 - void* memcpy(dst, src, n) */
    0x48, 0x89, 0xF8, 0x48, 0x89, 0xD1, 0xF3, 0xA4, 0xC3,
    /* memset: 92 - void* memset(ptr, val, n) */
    0x48, 0x57, 0x42, 0x8A, 0xC6, 0x48, 0x89, 0xD1, 0xF3, 0xAA, 0x58, 0xC3,
    /* atoi: 106 - int atoi(const char* str), positive only */
    0x31, 0xC0, 0x0F, 0xB6, 0x0F, 0x80, 0xF9, 0x30, 0x7C, 0x0F, 0x80, 0xF9, 0x39, 0x7F,
    0x0A, 0x8D, 0x04, 0x80, 0x01, 0xC0, 0x83, 0xE9, 0x30, 0x01, 0xC8, 0x48, 0xFF, 0xC7,
    0xEB, 0xE4, 0xC3
};

/* libgui: gui_put_pixel, gui_fill_rect, gui_draw_text, gui_clear, gui_width, gui_height */
static const unsigned char libgui_code[] = {
    0xB8, 0x06, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x07, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x08, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x09, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x0A, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x0B, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3
};

/* liblafillo: HTML->text via SYS_LAFILLO_HTML=19 */
static const unsigned char liblafillo_code[] = {
    0xB8, 0x13, 0x00, 0x00, 0x00,  /* mov eax, 19 */
    0x4C, 0x89, 0xCA,               /* mov r10, rcx */
    0xCD, 0x80,                     /* int 0x80 */
    0xC3                            /* ret */
};

/* libldll: ldll_load(name)->handle, ldll_sym(handle,name)->ptr, ldll_close(handle) */
static const unsigned char libldll_code[] = {
    /* ldll_load: rdi=name, rax=handle or -1 */
    0xB8, 0x03, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    /* ldll_sym: rdi=handle, rsi=name, rax=ptr or 0 */
    0xB8, 0x04, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    /* ldll_close: rdi=handle */
    0xB8, 0x05, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
};

/* libhash: hash_crc32(data,len)->crc, hash_fnv1a(data,len)->hash */
static const unsigned char libhash_code[] = {
    0xB8, 0x14, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x15, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
};

/* libbase64: base64_encode(in,in_len,out,out_cap)->n, base64_decode(in,in_len,out)->n */
static const unsigned char libbase64_code[] = {
    0xB8, 0x16, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xCA, 0xCD, 0x80, 0xC3,
    0xB8, 0x17, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
};

/* libfs: fs_open(path)->fd, fs_read(fd,buf,len)->n, fs_close(fd) */
static const unsigned char libfs_code[] = {
    0xB8, 0x0C, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x0D, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
    0xB8, 0x0E, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xC3,
};

static int write_embed(const char* path, const unsigned char* ldll, size_t len,
                      const char* var_name, const char* size_name) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "/* Generated by mkldll.c - do not edit */\n");
    fprintf(f, "#define %s %zu\n", size_name, len);
    fprintf(f, "static const uint8_t %s[%s] = {\n", var_name, size_name);
    for (size_t i = 0; i < len; ) {
        fputs("    ", f);
        for (int j = 0; j < 12 && i < len; j++, i++) {
            if (j) fputc(',', f);
            fprintf(f, "0x%02x", ldll[i]);
        }
        fputs(",\n", f);
    }
    fprintf(f, "};\n");
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    const char* lib = NULL;
    const char* out_path = NULL;
    const char* embed_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) out_path = argv[++i];
        } else if (strcmp(argv[i], "--embed") == 0) {
            if (i + 1 < argc) embed_path = argv[++i];
        } else if (strcmp(argv[i], "lard") == 0 || strcmp(argv[i], "gui") == 0 || strcmp(argv[i], "lafillo") == 0 || strcmp(argv[i], "ldll") == 0 || strcmp(argv[i], "hash") == 0 || strcmp(argv[i], "base64") == 0 || strcmp(argv[i], "fs") == 0) {
            lib = argv[i];
        }
    }

    if (!lib) {
        fprintf(stderr, "Usage: mkldll lard|gui|lafillo|ldll|hash|base64|fs -o out.ldll [--embed embed.inc]\n");
        return 1;
    }

    unsigned char buf[512];
    size_t len;
    const char* def_out;
    const char* embed_var;
    const char* size_var;

    if (strcmp(lib, "lard") == 0) {
        static const export_t exp[] = {
            {"puts", 0},
            {"strlen", 37},
            {"putc", 50},
            {"memcpy", 82},
            {"memset", 92},
            {"atoi", 106},
        };
        len = build_ldll(buf, sizeof(buf), liblard_code, sizeof(liblard_code), exp, 6);
        def_out = "liblard.ldll";
        embed_var = "file_liblard_ldll";
        size_var = "LDLL_LIBLARD_SIZE";
    } else if (strcmp(lib, "lafillo") == 0) {
        static const export_t exp[] = {
            {"lafillo_http_to_text", 0},
        };
        len = build_ldll(buf, sizeof(buf), liblafillo_code, sizeof(liblafillo_code), exp, 1);
        def_out = "liblafillo.ldll";
        embed_var = "file_liblafillo_ldll";
        size_var = "LDLL_LIBLAFILLO_SIZE";
    } else if (strcmp(lib, "ldll") == 0) {
        static const export_t exp[] = {
            {"ldll_load", 0},
            {"ldll_sym", 8},
            {"ldll_close", 16},
        };
        len = build_ldll(buf, sizeof(buf), libldll_code, sizeof(libldll_code), exp, 3);
        def_out = "libldll.ldll";
        embed_var = "file_libldll_ldll";
        size_var = "LDLL_LIBLDLL_SIZE";
    } else if (strcmp(lib, "hash") == 0) {
        static const export_t exp[] = {
            {"hash_crc32", 0},
            {"hash_fnv1a", 8},
        };
        len = build_ldll(buf, sizeof(buf), libhash_code, sizeof(libhash_code), exp, 2);
        def_out = "libhash.ldll";
        embed_var = "file_libhash_ldll";
        size_var = "LDLL_LIBHASH_SIZE";
    } else if (strcmp(lib, "base64") == 0) {
        static const export_t exp[] = {
            {"base64_encode", 0},
            {"base64_decode", 11},
        };
        len = build_ldll(buf, sizeof(buf), libbase64_code, sizeof(libbase64_code), exp, 2);
        def_out = "libbase64.ldll";
        embed_var = "file_libbase64_ldll";
        size_var = "LDLL_LIBBASE64_SIZE";
    } else if (strcmp(lib, "fs") == 0) {
        static const export_t exp[] = {
            {"fs_open", 0},
            {"fs_read", 8},
            {"fs_close", 16},
        };
        len = build_ldll(buf, sizeof(buf), libfs_code, sizeof(libfs_code), exp, 3);
        def_out = "libfs.ldll";
        embed_var = "file_libfs_ldll";
        size_var = "LDLL_LIBFS_SIZE";
    } else {
        static const export_t exp[] = {
            {"gui_put_pixel", 0},
            {"gui_fill_rect", 8},
            {"gui_draw_text", 17},
            {"gui_clear", 26},
            {"gui_width", 35},
            {"gui_height", 44},
        };
        len = build_ldll(buf, sizeof(buf), libgui_code, sizeof(libgui_code), exp, 6);
        def_out = "libgui.ldll";
        embed_var = "file_libgui_ldll";
        size_var = "LDLL_LIBGUI_SIZE";
    }

    const char* out = out_path ? out_path : def_out;
    FILE* f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "mkldll: cannot write %s\n", out);
        return 1;
    }
    fwrite(buf, 1, len, f);
    fclose(f);
    printf("Wrote %s (%zu bytes)\n", out, len);

    if (embed_path) {
        if (write_embed(embed_path, buf, len, embed_var, size_var) != 0) {
            fprintf(stderr, "mkldll: cannot write %s\n", embed_path);
            return 1;
        }
        printf("Wrote %s\n", embed_path);
    }
    return 0;
}
