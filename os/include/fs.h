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
const FsFile* fs_open_readonly(const char* name);
uint32_t fs_read(const FsFile* file, uint32_t offset, uint8_t* buf, uint32_t len);
void fs_list(void (*cb)(const char* name, uint32_t size, void* user), void* user);
void fs_list_readonly(void (*cb)(const char* name, uint32_t size, void* user), void* user);
void fs_list_writable(void (*cb)(const char* name, uint32_t size, void* user), void* user);
uint32_t fs_writable_count(void);
uint32_t fs_readonly_hidden_count(void);

/* Writable RAM files (notes, temp). Returns NULL if not found. */
FsWritableFile* fs_open_writable(const char* name);
uint32_t fs_write(FsWritableFile* f, uint32_t offset, const uint8_t* buf, uint32_t len);
uint32_t fs_append(FsWritableFile* f, const uint8_t* buf, uint32_t len);

/* User-owned delete overlay for read-only built-in/LFS files. */
int fs_hide_readonly(const char* name);
int fs_unhide_readonly(const char* name);
int fs_readonly_hidden(const char* name);
int fs_delete_overlay_selftest(void);

/* Persistent LPST store on the boot block device. */
int fs_persist_load(void);
int fs_persist_save(void);
void fs_persist_info(uint32_t* available, uint32_t* dirty, int* last_result,
                     const char** driver, uint32_t* lba, uint32_t* sectors);
void fs_persist_detail(uint32_t* active_bank, uint32_t* generation, uint32_t* bank_sectors);
void fs_mark_dirty(void);
