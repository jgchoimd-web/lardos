#include "megaclip.h"

#include <stddef.h>
#include <stdint.h>

static megaclip_item_t s_slots[MEGACLIP_SLOTS];
static uint32_t s_mode;
static uint32_t s_seq;
static uint32_t s_pushes;
static uint32_t s_pulls;
static uint32_t s_dropped;
static uint32_t s_last_error;

static uint32_t slen(const char* s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void copy_text(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (src && src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void clear_slot(megaclip_item_t* item)
{
    if (!item) return;
    item->used = 0;
    item->slot = 0;
    item->sequence = 0;
    item->size = 0;
    item->kind[0] = '\0';
    item->label[0] = '\0';
    item->data[0] = 0;
}

static void assign_slot(uint32_t slot, const char* kind, const char* label,
                        const uint8_t* data, uint32_t size)
{
    megaclip_item_t* item = &s_slots[slot];
    uint32_t n = size;
    if (n > MEGACLIP_DATA_MAX) {
        n = MEGACLIP_DATA_MAX;
        s_dropped++;
    }
    item->used = 1;
    item->slot = slot;
    item->sequence = ++s_seq;
    item->size = n;
    copy_text(item->kind, sizeof(item->kind), kind && kind[0] ? kind : "blob");
    copy_text(item->label, sizeof(item->label), label && label[0] ? label : "clipboard");
    for (uint32_t i = 0; i < n; i++) item->data[i] = data ? data[i] : 0;
    item->data[n] = 0;
}

static int newest_slot(void)
{
    uint32_t best_seq = 0;
    int best = -1;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].sequence >= best_seq) {
            best_seq = s_slots[i].sequence;
            best = (int)i;
        }
    }
    return best;
}

static int oldest_or_empty_slot(void)
{
    uint32_t best_seq = 0xFFFFFFFFu;
    int best = 0;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) {
        if (!s_slots[i].used) return (int)i;
        if (s_slots[i].sequence < best_seq) {
            best_seq = s_slots[i].sequence;
            best = (int)i;
        }
    }
    s_dropped++;
    return best;
}

static int order_view_slot(uint32_t view_index)
{
    uint32_t rank = 0;
    uint32_t last_seq = 0;
    for (;;) {
        uint32_t best_seq = 0xFFFFFFFFu;
        int best = -1;
        for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) {
            if (s_slots[i].used && s_slots[i].sequence > last_seq && s_slots[i].sequence < best_seq) {
                best_seq = s_slots[i].sequence;
                best = (int)i;
            }
        }
        if (best < 0) return -1;
        if (rank == view_index) return best;
        rank++;
        last_seq = best_seq;
    }
}

static int stack_view_slot(uint32_t view_index)
{
    if (view_index >= MEGACLIP_SLOTS || !s_slots[view_index].used) return -1;
    return (int)view_index;
}

static int view_slot(uint32_t view_index)
{
    if (view_index >= MEGACLIP_SLOTS) return -1;
    if (s_mode == MEGACLIP_MODE_SINGLE) return view_index == 0 && s_slots[0].used ? 0 : -1;
    if (s_mode == MEGACLIP_MODE_ORDER) return order_view_slot(view_index);
    return stack_view_slot(view_index);
}

void megaclip_init(void)
{
    s_mode = MEGACLIP_MODE_STACK;
    s_seq = 0;
    s_pushes = 0;
    s_pulls = 0;
    s_dropped = 0;
    s_last_error = 0;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) clear_slot(&s_slots[i]);
}

int megaclip_set_mode(uint32_t mode)
{
    if (mode > MEGACLIP_MODE_ORDER) {
        s_last_error = 1;
        return -1;
    }
    s_mode = mode;
    s_last_error = 0;
    return 0;
}

uint32_t megaclip_mode(void)
{
    return s_mode;
}

const char* megaclip_mode_name(uint32_t mode)
{
    if (mode == MEGACLIP_MODE_SINGLE) return "single";
    if (mode == MEGACLIP_MODE_ORDER) return "order";
    return "stack";
}

int megaclip_push(const char* kind, const char* label, const uint8_t* data, uint32_t size)
{
    if (!data && size) {
        s_last_error = 2;
        return -1;
    }
    if (s_mode == MEGACLIP_MODE_SINGLE) {
        for (uint32_t i = 1; i < MEGACLIP_SLOTS; i++) clear_slot(&s_slots[i]);
        assign_slot(0, kind, label, data, size);
    } else if (s_mode == MEGACLIP_MODE_ORDER) {
        assign_slot((uint32_t)oldest_or_empty_slot(), kind, label, data, size);
    } else {
        for (uint32_t i = MEGACLIP_SLOTS - 1u; i > 0; i--) s_slots[i] = s_slots[i - 1u];
        assign_slot(0, kind, label, data, size);
    }
    s_pushes++;
    s_last_error = 0;
    return 0;
}

int megaclip_push_text(const char* label, const char* text)
{
    return megaclip_push("text", label, (const uint8_t*)text, slen(text));
}

int megaclip_pull(uint32_t view_index, megaclip_item_t* out)
{
    int slot = view_slot(view_index);
    if (slot < 0 || !out) {
        s_last_error = 3;
        return -1;
    }
    *out = s_slots[(uint32_t)slot];
    out->slot = view_index;
    s_pulls++;
    s_last_error = 0;
    return 0;
}

int megaclip_pull_latest(megaclip_item_t* out)
{
    int slot = newest_slot();
    if (slot < 0 || !out) {
        s_last_error = 3;
        return -1;
    }
    *out = s_slots[(uint32_t)slot];
    out->slot = 0;
    s_pulls++;
    s_last_error = 0;
    return 0;
}

uint32_t megaclip_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) if (s_slots[i].used) count++;
    return count;
}

void megaclip_clear(void)
{
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) clear_slot(&s_slots[i]);
    s_last_error = 0;
}

void megaclip_status(megaclip_status_t* out)
{
    if (!out) return;
    out->mode = s_mode;
    out->count = megaclip_count();
    out->capacity = MEGACLIP_SLOTS;
    out->pushes = s_pushes;
    out->pulls = s_pulls;
    out->dropped = s_dropped;
    out->last_error = s_last_error;
}

int megaclip_selftest(void)
{
    megaclip_item_t item;
    uint32_t old_mode = s_mode;
    uint32_t old_seq = s_seq;
    uint32_t old_pushes = s_pushes;
    uint32_t old_pulls = s_pulls;
    uint32_t old_dropped = s_dropped;
    uint32_t old_last_error = s_last_error;
    megaclip_item_t old_slots[MEGACLIP_SLOTS];
    int ok = 1;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) old_slots[i] = s_slots[i];
    megaclip_init();
    if (megaclip_push_text("a", "one") != 0) ok = 0;
    if (ok && megaclip_push_text("b", "two") != 0) ok = 0;
    if (ok && megaclip_pull(0, &item) != 0) ok = 0;
    if (ok && (item.size != 3u || item.data[0] != 't' || item.data[1] != 'w')) ok = 0;
    if (ok && megaclip_pull(1, &item) != 0) ok = 0;
    if (ok && (item.size != 3u || item.data[0] != 'o')) ok = 0;
    if (ok && megaclip_set_mode(MEGACLIP_MODE_SINGLE) != 0) ok = 0;
    if (ok && megaclip_push_text("single", "only") != 0) ok = 0;
    if (ok && megaclip_count() != 1u) ok = 0;
    megaclip_clear();
    if (ok && megaclip_set_mode(MEGACLIP_MODE_ORDER) != 0) ok = 0;
    if (ok && megaclip_push_text("old", "first") != 0) ok = 0;
    if (ok && megaclip_push_text("new", "second") != 0) ok = 0;
    if (ok && megaclip_pull(0, &item) != 0) ok = 0;
    if (ok && item.data[0] != 'o') ok = 0;
    if (ok && megaclip_pull_latest(&item) != 0) ok = 0;
    if (ok && item.data[0] != 's') ok = 0;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) s_slots[i] = old_slots[i];
    s_mode = old_mode;
    s_seq = old_seq;
    s_pushes = old_pushes;
    s_pulls = old_pulls;
    s_dropped = old_dropped;
    s_last_error = old_last_error;
    return ok ? 0 : -1;
}
