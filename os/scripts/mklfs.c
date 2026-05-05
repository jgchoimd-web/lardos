/*
 * mklfs - Build LFS (Lard File System) image from files.
 * Replaces mklfs.py. No Python required.
 *
 * Usage: mklfs -o output.bin file1 [file2 ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

static const char* basename(const char* path) {
    const char* p = strrchr(path, '/');
    if (p) return p + 1;
    p = strrchr(path, '\\');
    if (p) return p + 1;
    return path;
}

typedef struct {
    const char* path;
    char* name;
    unsigned char* data;
    uint32_t size;
} file_entry_t;

int main(int argc, char** argv) {
    const char* out_path = NULL;
    int files_start = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "mklfs: -o requires path\n");
                return 1;
            }
            out_path = argv[++i];
        } else if (argv[i][0] != '-') {
            files_start = i;
            break;
        }
    }

    if (!out_path || files_start == 0 || files_start >= argc) {
        fprintf(stderr, "Usage: mklfs -o output.bin file1 [file2 ...]\n");
        return 1;
    }

    int nfiles = argc - files_start;
    file_entry_t* entries = (file_entry_t*)calloc((size_t)nfiles, sizeof(file_entry_t));
    if (!entries) return 1;

    size_t data_off = 8;
    for (int i = 0; i < nfiles; i++) {
        const char* path = argv[files_start + i];
        entries[i].path = path;
        entries[i].name = (char*)basename(path);
        size_t nlen = strlen(entries[i].name);
        if (nlen > 255) {
            fprintf(stderr, "mklfs: name too long: %s\n", path);
            free(entries);
            return 1;
        }
        data_off += 1 + nlen + 4 + 4;
    }

    for (int i = 0; i < nfiles; i++) {
        FILE* f = fopen(entries[i].path, "rb");
        if (!f) {
            fprintf(stderr, "mklfs: cannot open %s\n", entries[i].path);
            for (int j = 0; j < i; j++) free(entries[j].data);
            free(entries);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) {
            fclose(f);
            entries[i].data = NULL;
            entries[i].size = 0;
            continue;
        }
        entries[i].data = (unsigned char*)malloc((size_t)sz);
        if (!entries[i].data) {
            fclose(f);
            for (int j = 0; j < i; j++) free(entries[j].data);
            free(entries);
            return 1;
        }
        fread(entries[i].data, 1, (size_t)sz, f);
        fclose(f);
        entries[i].size = (uint32_t)sz;
    }

    size_t total_size = data_off;
    for (int i = 0; i < nfiles; i++) total_size += entries[i].size;

    unsigned char* out = (unsigned char*)malloc(total_size);
    if (!out) {
        for (int i = 0; i < nfiles; i++) free(entries[i].data);
        free(entries);
        return 1;
    }

    size_t pos = 0;
    memcpy(out + pos, "LFS\0", 4);
    pos += 4;
    out[pos++] = 1;
    out[pos++] = 0;
    out[pos++] = 0;
    out[pos++] = 0;
    write_u16(out + pos, (uint16_t)nfiles);
    pos += 2;

    uint32_t cur_off = (uint32_t)data_off;
    for (int i = 0; i < nfiles; i++) {
        size_t nlen = strlen(entries[i].name);
        out[pos++] = (unsigned char)nlen;
        memcpy(out + pos, entries[i].name, nlen);
        pos += nlen;
        write_u32(out + pos, cur_off);
        write_u32(out + pos + 4, entries[i].size);
        pos += 8;
        cur_off += entries[i].size;
    }

    for (int i = 0; i < nfiles; i++) {
        memcpy(out + pos, entries[i].data, entries[i].size);
        pos += entries[i].size;
        free(entries[i].data);
    }
    free(entries);

    FILE* of = fopen(out_path, "wb");
    if (!of) {
        fprintf(stderr, "mklfs: cannot write %s\n", out_path);
        free(out);
        return 1;
    }
    fwrite(out, 1, total_size, of);
    fclose(of);
    free(out);

    printf("Wrote %s (%zu bytes, %d files)\n", out_path, total_size, nfiles);
    return 0;
}
