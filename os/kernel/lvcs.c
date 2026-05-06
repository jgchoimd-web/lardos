#include "lvcs.h"

#include "string.h"

#include <stddef.h>

#define LVCS_MAX_STAGE 8
#define LVCS_MAX_COMMITS 8

typedef struct {
    char name[LVCS_MAX_NAME];
    uint32_t offset;
    uint32_t size;
    uint32_t hash;
} LvcsFileRef;

typedef struct {
    uint32_t id;
    uint32_t parent;
    uint32_t hash;
    uint32_t file_count;
    char message[LVCS_MAX_MESSAGE];
    LvcsFileRef files[LVCS_MAX_STAGE];
} LvcsCommit;

static uint8_t s_store[LVCS_STORE_CAP];
static uint32_t s_store_used;

static LvcsFileRef s_stage[LVCS_MAX_STAGE];
static uint32_t s_stage_count;

static LvcsCommit s_commits[LVCS_MAX_COMMITS];
static uint32_t s_commit_count;
static uint32_t s_next_id;

static int copy_name(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || !src || !src[0] || cap == 0) return -1;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    if (src[i]) return -2;
    dst[i] = '\0';
    return 0;
}

static void copy_message(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!src || !src[0]) src = "checkpoint";
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int find_stage(const char* name)
{
    for (uint32_t i = 0; i < s_stage_count; i++) {
        if (strcmp(s_stage[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int find_commit(uint32_t id)
{
    for (uint32_t i = 0; i < s_commit_count; i++) {
        if (s_commits[i].id == id) return (int)i;
    }
    return -1;
}

static int store_blob(const uint8_t* data, uint32_t size, uint32_t* offset)
{
    if (!data && size != 0) return -1;
    if (size > LVCS_MAX_FILE_SIZE) return -2;
    if (size > LVCS_STORE_CAP - s_store_used) return -3;
    *offset = s_store_used;
    for (uint32_t i = 0; i < size; i++) s_store[s_store_used + i] = data[i];
    s_store_used += size;
    return 0;
}

void lvcs_init(void)
{
    s_store_used = 0;
    s_stage_count = 0;
    s_commit_count = 0;
    s_next_id = 1;
}

uint32_t lvcs_hash(const uint8_t* data, uint32_t size)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

int lvcs_add(const char* name, const uint8_t* data, uint32_t size)
{
    char clean_name[LVCS_MAX_NAME];
    uint32_t offset;
    int r;
    int slot;
    int new_slot = 0;

    if (!name || !name[0]) return -1;
    r = copy_name(clean_name, sizeof(clean_name), name);
    if (r != 0) return -21;

    slot = find_stage(clean_name);
    if (slot < 0) {
        if (s_stage_count >= LVCS_MAX_STAGE) return -20;
        slot = (int)s_stage_count;
        new_slot = 1;
    }

    r = store_blob(data, size, &offset);
    if (r != 0) return r - 10;

    copy_name(s_stage[slot].name, sizeof(s_stage[slot].name), clean_name);
    s_stage[slot].offset = offset;
    s_stage[slot].size = size;
    s_stage[slot].hash = lvcs_hash(data, size);
    if (new_slot) s_stage_count++;
    return 0;
}

int lvcs_commit(const char* message, uint32_t* out_id)
{
    LvcsCommit* c;
    uint32_t h;

    if (s_stage_count == 0) return -1;
    if (s_commit_count >= LVCS_MAX_COMMITS) return -2;

    c = &s_commits[s_commit_count];
    c->id = s_next_id++;
    c->parent = s_commit_count ? s_commits[s_commit_count - 1].id : 0;
    c->file_count = s_stage_count;
    copy_message(c->message, sizeof(c->message), message);

    h = 2166136261u ^ c->parent;
    for (uint32_t i = 0; i < s_stage_count; i++) {
        c->files[i] = s_stage[i];
        h ^= s_stage[i].hash;
        h *= 16777619u;
        h ^= s_stage[i].size;
        h *= 16777619u;
    }
    c->hash = h;

    if (out_id) *out_id = c->id;
    s_commit_count++;
    s_stage_count = 0;
    return 0;
}

int lvcs_log(lvcs_commit_cb cb, void* user)
{
    int emitted = 0;
    if (!cb) return 0;
    for (uint32_t i = s_commit_count; i > 0; i--) {
        LvcsCommit* c = &s_commits[i - 1];
        lvcs_commit_info_t info;
        info.id = c->id;
        info.parent = c->parent;
        info.file_count = c->file_count;
        info.hash = c->hash;
        info.message = c->message;
        cb(&info, user);
        emitted++;
    }
    return emitted;
}

int lvcs_commit_files(uint32_t id, lvcs_file_cb cb, void* user)
{
    int idx = find_commit(id);
    if (idx < 0) return -1;
    if (!cb) return 0;
    for (uint32_t i = 0; i < s_commits[idx].file_count; i++) {
        lvcs_file_info_t info;
        info.name = s_commits[idx].files[i].name;
        info.size = s_commits[idx].files[i].size;
        info.hash = s_commits[idx].files[i].hash;
        cb(&info, user);
    }
    return (int)s_commits[idx].file_count;
}

int lvcs_stage(lvcs_file_cb cb, void* user)
{
    if (!cb) return 0;
    for (uint32_t i = 0; i < s_stage_count; i++) {
        lvcs_file_info_t info;
        info.name = s_stage[i].name;
        info.size = s_stage[i].size;
        info.hash = s_stage[i].hash;
        cb(&info, user);
    }
    return (int)s_stage_count;
}

int lvcs_checkout(uint32_t id, const char* name, uint8_t* out, uint32_t* out_len)
{
    int idx = find_commit(id);
    if (idx < 0) return -1;
    if (!name || !name[0] || !out || !out_len) return -2;

    for (int ci = idx; ci >= 0; ci--) {
        LvcsCommit* c = &s_commits[ci];
        for (uint32_t fi = 0; fi < c->file_count; fi++) {
            LvcsFileRef* f = &c->files[fi];
            if (strcmp(f->name, name) == 0) {
                if (*out_len < f->size) return -3;
                for (uint32_t i = 0; i < f->size; i++) out[i] = s_store[f->offset + i];
                *out_len = f->size;
                return 0;
            }
        }
    }
    return -4;
}

void lvcs_status(uint32_t* staged, uint32_t* commits, uint32_t* used, uint32_t* cap)
{
    if (staged) *staged = s_stage_count;
    if (commits) *commits = s_commit_count;
    if (used) *used = s_store_used;
    if (cap) *cap = LVCS_STORE_CAP;
}
