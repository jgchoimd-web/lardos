#pragma once

#include <stdint.h>

typedef struct {
    char drive;
    const char* name;
    const char* label;
    const char* driver;
    uint32_t present;
    uint32_t persistent;
    uint32_t dirty;
    uint32_t files;
    uint32_t bytes;
    uint32_t lba;
    uint32_t sectors;
    int last_error;
} mediafs_info_t;

typedef void (*mediafs_list_cb)(const char* name, uint32_t size, void* user);

void mediafs_init(void);
uint32_t mediafs_count(void);
int mediafs_info(uint32_t idx, mediafs_info_t* out);
int mediafs_drive_supported(char drive);
int mediafs_list(char drive, mediafs_list_cb cb, void* user);
int mediafs_read(char drive, const char* name, const uint8_t** data, uint32_t* size);
int mediafs_write(char drive, const char* name, const uint8_t* data, uint32_t size, int append);
int mediafs_delete(char drive, const char* name);
int mediafs_format(char drive);
int mediafs_sync(char drive);
int mediafs_selftest(void);
