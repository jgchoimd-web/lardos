#pragma once

#include <stdint.h>

typedef struct FsFile {
    const char* name;
    const uint8_t* data;
    uint32_t size;
} FsFile;

typedef struct FsWritableFile {
    char name[32];
    uint8_t* data;
    uint32_t size;
    uint32_t cap;
} FsWritableFile;

void fs_init(void);
const FsFile* fs_open(const char* name);
uint32_t fs_read(const FsFile* file, uint32_t offset, uint8_t* buf, uint32_t len);
void fs_list(void (*cb)(const char* name, uint32_t size, void* user), void* user);

/* Writable RAM files (notes, temp). Returns NULL if not found. */
FsWritableFile* fs_open_writable(const char* name);
uint32_t fs_write(FsWritableFile* f, uint32_t offset, const uint8_t* buf, uint32_t len);
uint32_t fs_append(FsWritableFile* f, const uint8_t* buf, uint32_t len);

