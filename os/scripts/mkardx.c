/*
 * mkardx - Pack ELF into LARDX (LardOS executable) format.
 * Replaces mkardx.py. No Python required.
 * Supports ELF32 and ELF64 little-endian.
 *
 * Usage: mkardx input.elf output.bosx [--user]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAGIC "LARD"
#define VERSION 2
#define IMAGE_KERNEL 0
#define IMAGE_USER 1
#define PT_LOAD 1
#define PHDR_SIZE 20

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    uint32_t paddr;
    uint32_t file_off;
    uint32_t file_sz;
    uint32_t mem_sz;
    uint32_t flags;
} phdr_t;

typedef struct {
    uint32_t paddr;
    unsigned char* data;
    uint32_t data_len;
    uint32_t mem_sz;
    uint32_t flags;
} seg_t;

static uint16_t read_u16(const unsigned char* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const unsigned char* p) {
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

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

static int elf64_parse(const unsigned char* blob, size_t size, uint32_t* entry, seg_t* segs, int* seg_count) {
    if (size < 64) return -1;
    if (memcmp(blob, "\177" "ELF", 4) != 0) return -1;
    if (blob[4] != 2) return -1; /* ELF64 */
    if (blob[5] != 1) return -1; /* LE */

    uint16_t e_phnum = read_u16(blob + 56);
    uint16_t e_phentsize = read_u16(blob + 54);
    uint32_t e_phoff = (uint32_t)read_u64(blob + 40);
    uint64_t e_entry = read_u64(blob + 24);

    if (e_phoff == 0 || e_phnum == 0) return -1;
    if (e_phentsize < 56) return -1;

    int n = 0;
    for (uint16_t i = 0; i < e_phnum && n < 16; i++) {
        size_t off = e_phoff + i * e_phentsize;
        if (off + 56 > size) return -1;

        uint32_t p_type = read_u32(blob + off);
        uint32_t p_flags = read_u32(blob + off + 4);
        uint64_t p_offset = read_u64(blob + off + 8);
        uint64_t p_paddr = read_u64(blob + off + 24);
        uint64_t p_filesz = read_u64(blob + off + 32);
        uint64_t p_memsz = read_u64(blob + off + 40);

        if (p_type != PT_LOAD) continue;
        if (p_filesz == 0 && p_memsz == 0) continue;
        if (p_offset + p_filesz > size) return -1;
        if (p_paddr >= (1ULL << 32)) return -1;

        uint32_t flags = p_flags & 7;
        segs[n].paddr = (uint32_t)p_paddr;
        segs[n].data_len = (uint32_t)p_filesz;
        segs[n].mem_sz = (uint32_t)(p_memsz > p_filesz ? p_memsz : p_filesz);
        segs[n].flags = flags;
        segs[n].data = (unsigned char*)blob + p_offset;
        n++;
    }

    if (n == 0) return -1;
    if (e_entry >= (1ULL << 32)) return -1;
    *entry = (uint32_t)e_entry;
    *seg_count = n;
    return 0;
}

static int elf32_parse(const unsigned char* blob, size_t size, uint32_t* entry, seg_t* segs, int* seg_count) {
    if (size < 52) return -1;
    if (memcmp(blob, "\177" "ELF", 4) != 0) return -1;
    if (blob[4] != 1) return -1;
    if (blob[5] != 1) return -1;

    uint16_t e_phnum = read_u16(blob + 44);
    uint16_t e_phentsize = read_u16(blob + 42);
    uint32_t e_phoff = read_u32(blob + 28);
    uint32_t e_entry = read_u32(blob + 24);

    if (e_phoff == 0 || e_phnum == 0) return -1;
    if (e_phentsize < 32) return -1;

    int n = 0;
    for (uint16_t i = 0; i < e_phnum && n < 16; i++) {
        size_t off = e_phoff + i * e_phentsize;
        if (off + 32 > size) return -1;

        uint32_t p_type = read_u32(blob + off);
        uint32_t p_offset = read_u32(blob + off + 4);
        uint32_t p_paddr = read_u32(blob + off + 12);
        uint32_t p_filesz = read_u32(blob + off + 16);
        uint32_t p_memsz = read_u32(blob + off + 20);
        uint32_t p_flags = read_u32(blob + off + 24);

        if (p_type != PT_LOAD) continue;
        if (p_filesz == 0 && p_memsz == 0) continue;
        if (p_offset + p_filesz > size) return -1;

        segs[n].paddr = p_paddr;
        segs[n].data_len = p_filesz;
        segs[n].mem_sz = p_memsz > p_filesz ? p_memsz : p_filesz;
        segs[n].flags = p_flags & 7;
        segs[n].data = (unsigned char*)blob + p_offset;
        n++;
    }

    if (n == 0) return -1;
    *entry = e_entry;
    *seg_count = n;
    return 0;
}

/* Simple sort by paddr */
static void sort_segs(seg_t* segs, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (segs[j].paddr < segs[i].paddr) {
                seg_t t = segs[i];
                segs[i] = segs[j];
                segs[j] = t;
            }
        }
    }
}

static size_t build_lardx(unsigned char* out, size_t cap, uint32_t entry, seg_t* segs, int seg_count, int image_type) {
    size_t pos = 0;

    memcpy(out + pos, MAGIC, 4);
    pos += 4;
    write_u16(out + pos, VERSION);
    out[pos + 2] = (unsigned char)image_type;
    out[pos + 3] = 0;
    pos += 4;
    write_u16(out + pos, (uint16_t)seg_count);
    pos += 2;
    write_u32(out + pos, entry);
    pos += 4;
    write_u32(out + pos, 32);
    pos += 4;

    /* file_size at 0x12, placeholder */
    pos += 4;
    memset(out + pos, 0, 10);
    pos += 10;  /* total 0-22 done, reserve 10 more -> 32 */

    /* Program headers */
    size_t payload_start = 32 + seg_count * PHDR_SIZE;
    uint32_t cur_off = (uint32_t)payload_start;

    for (int i = 0; i < seg_count; i++) {
        write_u32(out + pos, segs[i].paddr);
        write_u32(out + pos + 4, cur_off);
        write_u32(out + pos + 8, segs[i].data_len);
        write_u32(out + pos + 12, segs[i].mem_sz);
        write_u32(out + pos + 16, segs[i].flags);
        pos += PHDR_SIZE;
        cur_off += segs[i].data_len;
    }

    /* Payload */
    for (int i = 0; i < seg_count; i++) {
        if (pos + segs[i].data_len <= cap)
            memcpy(out + pos, segs[i].data, segs[i].data_len);
        pos += segs[i].data_len;
    }

    /* Backfill file_size in header (offset 0x12) */
    write_u32(out + 18, (uint32_t)pos);
    return pos;
}

int main(int argc, char** argv) {
    const char* elf_path = NULL;
    const char* out_path = NULL;
    int user_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--user") == 0) {
            user_mode = 1;
        } else if (!elf_path) {
            elf_path = argv[i];
        } else if (!out_path) {
            out_path = argv[i];
        }
    }

    if (!elf_path || !out_path) {
        fprintf(stderr, "Usage: mkardx input.elf output.bosx [--user]\n");
        return 1;
    }

    FILE* f = fopen(elf_path, "rb");
    if (!f) {
        fprintf(stderr, "mkardx: cannot open %s\n", elf_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 16 * 1024 * 1024) {
        fclose(f);
        fprintf(stderr, "mkardx: invalid file size\n");
        return 1;
    }

    unsigned char* blob = (unsigned char*)malloc((size_t)fsize);
    if (!blob) {
        fclose(f);
        return 1;
    }
    size_t nread = fread(blob, 1, (size_t)fsize, f);
    fclose(f);
    if (nread != (size_t)fsize) {
        free(blob);
        fprintf(stderr, "mkardx: read error\n");
        return 1;
    }

    seg_t segs[16];
    int seg_count = 0;
    uint32_t entry;

    int r;
    if (blob[4] == 2) {
        r = elf64_parse(blob, nread, &entry, segs, &seg_count);
    } else if (blob[4] == 1) {
        r = elf32_parse(blob, nread, &entry, segs, &seg_count);
    } else {
        r = -1;
    }

    if (r != 0) {
        free(blob);
        fprintf(stderr, "mkardx: failed to parse ELF\n");
        return 1;
    }

    sort_segs(segs, seg_count);
    int img_type = user_mode ? IMAGE_USER : IMAGE_KERNEL;

    size_t out_cap = 1024 * 1024;
    unsigned char* out = (unsigned char*)malloc(out_cap);
    if (!out) {
        free(blob);
        return 1;
    }

    size_t out_len = build_lardx(out, out_cap, entry, segs, seg_count, img_type);

    FILE* of = fopen(out_path, "wb");
    if (!of) {
        free(blob);
        free(out);
        fprintf(stderr, "mkardx: cannot write %s\n", out_path);
        return 1;
    }
    fwrite(out, 1, out_len, of);
    fclose(of);
    free(blob);
    free(out);

    printf("Wrote %s (%zu bytes, %d segments)\n", out_path, out_len, seg_count);
    return 0;
}
