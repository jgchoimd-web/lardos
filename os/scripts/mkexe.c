/*
 * mkexe - Pack ELF into BOSX executable format.
 * Replaces mkexe.py. BOSX is a variant of LARDX with 16-byte phdrs.
 *
 * Usage: mkexe input.elf output.bosx
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PT_LOAD 1
#define PHDR_SIZE 16

static uint32_t read_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const unsigned char* p) {
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

static void write_u32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void write_u16(unsigned char* p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

typedef struct {
    uint32_t paddr;
    unsigned char* data;
    uint32_t data_len;
    uint32_t mem_sz;
} seg_t;

static int elf64_parse(const unsigned char* blob, size_t size, uint32_t* entry, seg_t* segs, int* seg_count) {
    if (size < 64) return -1;
    if (memcmp(blob, "\x7fELF", 4) != 0) return -1;
    if (blob[4] != 2 || blob[5] != 1) return -1;

    uint16_t e_phnum = (uint16_t)blob[56] | ((uint16_t)blob[57] << 8);
    uint16_t e_phentsize = (uint16_t)blob[54] | ((uint16_t)blob[55] << 8);
    uint64_t e_phoff = read_u64(blob + 40);
    uint64_t e_entry = read_u64(blob + 24);

    if (e_phoff == 0 || e_phnum == 0 || e_phentsize < 56) return -1;

    int n = 0;
    for (uint16_t i = 0; i < e_phnum && n < 16; i++) {
        size_t off = e_phoff + i * e_phentsize;
        if (off + 56 > size) return -1;

        uint32_t p_type = read_u32(blob + off);
        uint64_t p_offset = read_u64(blob + off + 8);
        uint64_t p_paddr = read_u64(blob + off + 24);
        uint64_t p_filesz = read_u64(blob + off + 32);
        uint64_t p_memsz = read_u64(blob + off + 40);

        if (p_type != PT_LOAD) continue;
        if (p_filesz == 0 && p_memsz == 0) continue;
        if (p_offset + p_filesz > size) return -1;
        if (p_paddr >= (1ULL << 32)) return -1;

        segs[n].paddr = (uint32_t)p_paddr;
        segs[n].data_len = (uint32_t)p_filesz;
        segs[n].mem_sz = (uint32_t)(p_memsz > p_filesz ? p_memsz : p_filesz);
        segs[n].data = (unsigned char*)blob + p_offset;
        n++;
    }

    if (n == 0 || e_entry >= (1ULL << 32)) return -1;
    *entry = (uint32_t)e_entry;
    *seg_count = n;
    return 0;
}

static int elf32_parse(const unsigned char* blob, size_t size, uint32_t* entry, seg_t* segs, int* seg_count) {
    if (size < 52) return -1;
    if (memcmp(blob, "\x7fELF", 4) != 0) return -1;
    if (blob[4] != 1 || blob[5] != 1) return -1;

    uint16_t e_phnum = (uint16_t)blob[44] | ((uint16_t)blob[45] << 8);
    uint16_t e_phentsize = (uint16_t)blob[42] | ((uint16_t)blob[43] << 8);
    uint32_t e_phoff = read_u32(blob + 28);
    uint32_t e_entry = read_u32(blob + 24);

    if (e_phoff == 0 || e_phnum == 0 || e_phentsize < 32) return -1;

    int n = 0;
    for (uint16_t i = 0; i < e_phnum && n < 16; i++) {
        size_t off = e_phoff + i * e_phentsize;
        if (off + 32 > size) return -1;

        uint32_t p_type = read_u32(blob + off);
        uint32_t p_offset = read_u32(blob + off + 4);
        uint32_t p_paddr = read_u32(blob + off + 12);
        uint32_t p_filesz = read_u32(blob + off + 16);
        uint32_t p_memsz = read_u32(blob + off + 20);

        if (p_type != PT_LOAD) continue;
        if (p_filesz == 0 && p_memsz == 0) continue;
        if (p_offset + p_filesz > size) return -1;

        segs[n].paddr = p_paddr;
        segs[n].data_len = p_filesz;
        segs[n].mem_sz = p_memsz > p_filesz ? p_memsz : p_filesz;
        segs[n].data = (unsigned char*)blob + p_offset;
        n++;
    }

    if (n == 0) return -1;
    *entry = e_entry;
    *seg_count = n;
    return 0;
}

static void sort_segs(seg_t* segs, int n) {
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (segs[j].paddr < segs[i].paddr) {
                seg_t t = segs[i];
                segs[i] = segs[j];
                segs[j] = t;
            }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: mkexe input.elf output.bosx\n");
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "mkexe: cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        return 1;
    }

    unsigned char* blob = (unsigned char*)malloc((size_t)sz);
    fread(blob, 1, (size_t)sz, f);
    fclose(f);

    seg_t segs[16];
    int seg_count = 0;
    uint32_t entry;
    int r = (blob[4] == 2) ? elf64_parse(blob, (size_t)sz, &entry, segs, &seg_count)
                            : elf32_parse(blob, (size_t)sz, &entry, segs, &seg_count);
    if (r != 0) {
        free(blob);
        fprintf(stderr, "mkexe: ELF parse failed\n");
        return 1;
    }

    sort_segs(segs, seg_count);

    size_t out_cap = 1024 * 1024;
    unsigned char* out = (unsigned char*)malloc(out_cap);
    if (!out) {
        free(blob);
        return 1;
    }

    memcpy(out, "BOSX", 4);
    write_u16(out + 4, 2);
    write_u16(out + 6, (uint16_t)seg_count);
    write_u32(out + 8, entry);
    write_u32(out + 12, 32);
    size_t payload_start = 32 + seg_count * PHDR_SIZE;
    uint32_t cur_off = (uint32_t)payload_start;

    for (int i = 0; i < seg_count; i++) {
        write_u32(out + 32 + i * PHDR_SIZE, segs[i].paddr);
        write_u32(out + 36 + i * PHDR_SIZE, cur_off);
        write_u32(out + 40 + i * PHDR_SIZE, segs[i].data_len);
        write_u32(out + 44 + i * PHDR_SIZE, segs[i].mem_sz);
        cur_off += segs[i].data_len;
    }
    write_u32(out + 16, cur_off);
    memset(out + 20, 0, 12);

    size_t pos = payload_start;
    for (int i = 0; i < seg_count; i++) {
        memcpy(out + pos, segs[i].data, segs[i].data_len);
        pos += segs[i].data_len;
    }

    FILE* of = fopen(argv[2], "wb");
    if (!of) {
        free(blob);
        free(out);
        return 1;
    }
    fwrite(out, 1, pos, of);
    fclose(of);
    free(blob);
    free(out);

    printf("Wrote %s (%zu bytes, %d segments)\n", argv[2], pos, seg_count);
    return 0;
}
