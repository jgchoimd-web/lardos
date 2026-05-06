#pragma once

#include <stdint.h>

#define LVCS_MAX_NAME 32
#define LVCS_MAX_MESSAGE 64
#define LVCS_MAX_FILE_SIZE 2048u
#define LVCS_STORE_CAP 32768u

typedef struct {
    uint32_t id;
    uint32_t parent;
    uint32_t file_count;
    uint32_t hash;
    const char* message;
} lvcs_commit_info_t;

typedef struct {
    const char* name;
    uint32_t size;
    uint32_t hash;
} lvcs_file_info_t;

typedef void (*lvcs_commit_cb)(const lvcs_commit_info_t* info, void* user);
typedef void (*lvcs_file_cb)(const lvcs_file_info_t* info, void* user);

void lvcs_init(void);
uint32_t lvcs_hash(const uint8_t* data, uint32_t size);
int lvcs_add(const char* name, const uint8_t* data, uint32_t size);
int lvcs_commit(const char* message, uint32_t* out_id);
int lvcs_log(lvcs_commit_cb cb, void* user);
int lvcs_commit_files(uint32_t id, lvcs_file_cb cb, void* user);
int lvcs_stage(lvcs_file_cb cb, void* user);
int lvcs_checkout(uint32_t id, const char* name, uint8_t* out, uint32_t* out_len);
void lvcs_status(uint32_t* staged, uint32_t* commits, uint32_t* used, uint32_t* cap);
