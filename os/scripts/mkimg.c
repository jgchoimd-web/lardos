/*
 * mkimg - Build raw disk image (replaces dd for os-image.bin).
 * Creates a BIOS boot image: boot at sector 0, optional stage2 at 1..8,
 * kernel after the loader area, persistent store near the end.
 *
 * Usage: mkimg [--sectors n] [--persist-start n] [--persist-sectors n]
 *              -o os-image.bin -b boot/stage1.bin [-s boot/stage2.bin] -k kernel/kernel.bosx
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define DEFAULT_SECTOR_COUNT 2880
#define STAGE2_SECTORS 8
#define DEFAULT_PERSIST_START_SECTOR 2752
#define DEFAULT_PERSIST_SECTORS 128

static int read_u32(const char* s, uint32_t* out)
{
    char* end = NULL;
    unsigned long v;
    if (!s || !out) return -1;
    v = strtoul(s, &end, 10);
    if (!end || *end != '\0' || v == 0 || v > 0xFFFFFFFFul) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int read_file(const char* path, unsigned char** buf, size_t* len, size_t max_size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t)sz > max_size) {
        fclose(f);
        return -1;
    }
    *buf = (unsigned char*)malloc((size_t)sz);
    if (!*buf) {
        fclose(f);
        return -1;
    }
    size_t n = fread(*buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(*buf);
        return -1;
    }
    *len = (size_t)sz;
    return 0;
}

int main(int argc, char** argv)
{
    const char* out_path = 0;
    const char* boot_path = 0;
    const char* stage2_path = 0;
    const char* kernel_path = 0;
    uint32_t sector_count = DEFAULT_SECTOR_COUNT;
    uint32_t persist_start_sector = DEFAULT_PERSIST_START_SECTOR;
    uint32_t persist_sectors = DEFAULT_PERSIST_SECTORS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            boot_path = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            stage2_path = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            kernel_path = argv[++i];
        } else if (strcmp(argv[i], "--sectors") == 0 && i + 1 < argc) {
            if (read_u32(argv[++i], &sector_count) != 0) {
                fprintf(stderr, "mkimg: invalid --sectors\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--persist-start") == 0 && i + 1 < argc) {
            if (read_u32(argv[++i], &persist_start_sector) != 0) {
                fprintf(stderr, "mkimg: invalid --persist-start\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--persist-sectors") == 0 && i + 1 < argc) {
            if (read_u32(argv[++i], &persist_sectors) != 0) {
                fprintf(stderr, "mkimg: invalid --persist-sectors\n");
                return 1;
            }
        }
    }

    if (!out_path || !boot_path || !kernel_path) {
        fprintf(stderr, "Usage: mkimg [--sectors n] [--persist-start n] [--persist-sectors n] -o <image.bin> -b <stage1.bin> [-s <stage2.bin>] -k <kernel.bosx>\n");
        return 1;
    }
    if (sector_count < 1u + STAGE2_SECTORS + 1u ||
        persist_start_sector <= 1u + STAGE2_SECTORS ||
        persist_start_sector + persist_sectors > sector_count) {
        fprintf(stderr, "mkimg: invalid boot image layout\n");
        return 1;
    }
    size_t image_size = (size_t)SECTOR_SIZE * (size_t)sector_count;

    unsigned char* boot_buf = 0;
    unsigned char* stage2_buf = 0;
    unsigned char* kernel_buf = 0;
    size_t boot_len = 0, stage2_len = 0, kernel_len = 0;

    if (read_file(boot_path, &boot_buf, &boot_len, image_size) != 0) {
        fprintf(stderr, "mkimg: cannot read %s\n", boot_path);
        return 1;
    }
    if (boot_len > SECTOR_SIZE) {
        fprintf(stderr, "mkimg: boot larger than one sector (%zu bytes)\n", boot_len);
        free(boot_buf);
        return 1;
    }

    if (stage2_path && read_file(stage2_path, &stage2_buf, &stage2_len, image_size) != 0) {
        fprintf(stderr, "mkimg: cannot read %s\n", stage2_path);
        free(boot_buf);
        return 1;
    }
    if (stage2_len > STAGE2_SECTORS * (size_t)SECTOR_SIZE) {
        fprintf(stderr, "mkimg: stage2 larger than %d sectors (%zu bytes)\n", STAGE2_SECTORS, stage2_len);
        free(boot_buf);
        free(stage2_buf);
        return 1;
    }

    if (read_file(kernel_path, &kernel_buf, &kernel_len, image_size) != 0) {
        fprintf(stderr, "mkimg: cannot read %s\n", kernel_path);
        free(boot_buf);
        free(stage2_buf);
        return 1;
    }
    size_t kernel_off = stage2_path ? (1 + STAGE2_SECTORS) * (size_t)SECTOR_SIZE : SECTOR_SIZE;
    size_t persist_off = (size_t)persist_start_sector * SECTOR_SIZE;
    if (kernel_len > persist_off - kernel_off) {
        fprintf(stderr, "mkimg: kernel too large for reserved LPST persistent store\n");
        free(boot_buf);
        free(stage2_buf);
        free(kernel_buf);
        return 1;
    }

    unsigned char* img = (unsigned char*)calloc(1, image_size);
    if (!img) {
        fprintf(stderr, "mkimg: out of memory\n");
        free(boot_buf);
        free(stage2_buf);
        free(kernel_buf);
        return 1;
    }

    memcpy(img, boot_buf, boot_len);
    if (stage2_buf) {
        memcpy(img + SECTOR_SIZE, stage2_buf, stage2_len);
    }
    memcpy(img + kernel_off, kernel_buf, kernel_len);

    FILE* f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "mkimg: cannot write %s\n", out_path);
        free(img);
        free(boot_buf);
        free(stage2_buf);
        free(kernel_buf);
        return 1;
    }
    size_t written = fwrite(img, 1, image_size, f);
    fclose(f);
    free(img);
    free(boot_buf);
    free(stage2_buf);
    free(kernel_buf);

    if (written != image_size) {
        fprintf(stderr, "mkimg: write failed\n");
        return 1;
    }
    return 0;
}
