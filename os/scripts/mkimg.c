/*
 * mkimg - Build raw disk image (replaces dd for os-image.bin).
 * Creates 2880-sector floppy image: boot at sector 0, optional stage2 at 1..8,
 * kernel after the loader area.
 *
 * Usage: mkimg -o os-image.bin -b boot/stage1.bin [-s boot/stage2.bin] -k kernel/kernel.bosx
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define SECTOR_COUNT 2880
#define STAGE2_SECTORS 8
#define PERSIST_START_SECTOR 2752
#define PERSIST_SECTORS 128
#define IMAGE_SIZE ((size_t)SECTOR_SIZE * SECTOR_COUNT)

static int read_file(const char* path, unsigned char** buf, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (long)IMAGE_SIZE) {
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

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            boot_path = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            stage2_path = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            kernel_path = argv[++i];
        }
    }

    if (!out_path || !boot_path || !kernel_path) {
        fprintf(stderr, "Usage: mkimg -o <image.bin> -b <stage1.bin> [-s <stage2.bin>] -k <kernel.bosx>\n");
        return 1;
    }

    unsigned char* boot_buf = 0;
    unsigned char* stage2_buf = 0;
    unsigned char* kernel_buf = 0;
    size_t boot_len = 0, stage2_len = 0, kernel_len = 0;

    if (read_file(boot_path, &boot_buf, &boot_len) != 0) {
        fprintf(stderr, "mkimg: cannot read %s\n", boot_path);
        return 1;
    }
    if (boot_len > SECTOR_SIZE) {
        fprintf(stderr, "mkimg: boot larger than one sector (%zu bytes)\n", boot_len);
        free(boot_buf);
        return 1;
    }

    if (stage2_path && read_file(stage2_path, &stage2_buf, &stage2_len) != 0) {
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

    if (read_file(kernel_path, &kernel_buf, &kernel_len) != 0) {
        fprintf(stderr, "mkimg: cannot read %s\n", kernel_path);
        free(boot_buf);
        free(stage2_buf);
        return 1;
    }
    size_t kernel_off = stage2_path ? (1 + STAGE2_SECTORS) * (size_t)SECTOR_SIZE : SECTOR_SIZE;
    size_t persist_off = (size_t)PERSIST_START_SECTOR * SECTOR_SIZE;
    if (kernel_len > persist_off - kernel_off) {
        fprintf(stderr, "mkimg: kernel too large for reserved LPST persistent store\n");
        free(boot_buf);
        free(stage2_buf);
        free(kernel_buf);
        return 1;
    }

    unsigned char* img = (unsigned char*)calloc(1, IMAGE_SIZE);
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
    size_t written = fwrite(img, 1, IMAGE_SIZE, f);
    fclose(f);
    free(img);
    free(boot_buf);
    free(stage2_buf);
    free(kernel_buf);

    if (written != IMAGE_SIZE) {
        fprintf(stderr, "mkimg: write failed\n");
        return 1;
    }
    return 0;
}
