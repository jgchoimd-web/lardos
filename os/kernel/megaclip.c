#include "megaclip.h"
#include "fs.h"

#include <stddef.h>
#include <stdint.h>

static megaclip_item_t s_slots[MEGACLIP_SLOTS];
static megaclip_item_t s_pins[MEGACLIP_PIN_SLOTS];
static uint32_t s_mode;
static uint32_t s_seq;
static uint32_t s_pushes;
static uint32_t s_pulls;
static uint32_t s_dropped;
static uint32_t s_last_error;
static uint32_t s_pin_sets;
static uint32_t s_pin_pulls;

#define MEGACLIP_DOC_CAP 8192u
static char s_doc_buf[MEGACLIP_DOC_CAP];

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

static uint32_t pin_slot_number(uint32_t slot)
{
    return slot == 9u ? 0u : slot + 1u;
}

static int parse_slot_number(uint32_t n, uint32_t* out)
{
    if (!out) return -1;
    if (n == 0u) {
        *out = 9u;
        return 0;
    }
    if (n >= 1u && n <= 10u) {
        *out = n - 1u;
        return 0;
    }
    return -1;
}

static void doc_append(char* dst, uint32_t cap, uint32_t* pos, const char* text)
{
    if (!dst || !pos || !text || cap == 0) return;
    while (*text && *pos + 1u < cap) {
        dst[*pos] = *text;
        (*pos)++;
        text++;
    }
    if (*pos < cap) dst[*pos] = '\0';
}

static void doc_append_char(char* dst, uint32_t cap, uint32_t* pos, char ch)
{
    if (!dst || !pos || cap == 0) return;
    if (*pos + 1u < cap) {
        dst[*pos] = ch;
        (*pos)++;
        dst[*pos] = '\0';
    }
}

static void doc_append_u32(char* dst, uint32_t cap, uint32_t* pos, uint32_t value)
{
    char tmp[11];
    uint32_t n = 0;
    if (value == 0u) {
        doc_append_char(dst, cap, pos, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n) doc_append_char(dst, cap, pos, tmp[--n]);
}

static void doc_append_clean_data(char* dst, uint32_t cap, uint32_t* pos,
                                  const uint8_t* data, uint32_t size)
{
    for (uint32_t i = 0; i < size && *pos + 1u < cap; i++) {
        char ch = (char)data[i];
        if (ch == '\n' || ch == '\r') ch = ' ';
        if (ch < ' ' || ch > '~') ch = '.';
        doc_append_char(dst, cap, pos, ch);
    }
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

static void reset_runtime(uint32_t include_pins)
{
    s_mode = MEGACLIP_MODE_STACK;
    s_seq = 0;
    s_pushes = 0;
    s_pulls = 0;
    s_dropped = 0;
    s_last_error = 0;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) clear_slot(&s_slots[i]);
    if (include_pins) {
        s_pin_sets = 0;
        s_pin_pulls = 0;
        for (uint32_t i = 0; i < MEGACLIP_PIN_SLOTS; i++) clear_slot(&s_pins[i]);
    }
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

static void assign_pin_raw(uint32_t slot, const char* kind, const char* label,
                           const uint8_t* data, uint32_t size)
{
    megaclip_item_t* item;
    uint32_t n = size;
    if (slot >= MEGACLIP_PIN_SLOTS) return;
    item = &s_pins[slot];
    if (n > MEGACLIP_DATA_MAX) {
        n = MEGACLIP_DATA_MAX;
        s_dropped++;
    }
    item->used = 1;
    item->slot = slot;
    item->sequence = ++s_seq;
    item->size = n;
    copy_text(item->kind, sizeof(item->kind), kind && kind[0] ? kind : "text");
    copy_text(item->label, sizeof(item->label), label && label[0] ? label : "fixed");
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

static int line_prefix_len(const uint8_t* data, uint32_t start, uint32_t end,
                           const char* prefix, uint32_t* len)
{
    uint32_t i = 0;
    while (prefix && prefix[i]) {
        if (start + i >= end || (char)data[start + i] != prefix[i]) return 0;
        i++;
    }
    if (len) *len = i;
    return 1;
}

static uint32_t line_skip_spaces(const uint8_t* data, uint32_t p, uint32_t end)
{
    while (p < end && (data[p] == ' ' || data[p] == '\t')) p++;
    return p;
}

static uint32_t line_read_word(const uint8_t* data, uint32_t p, uint32_t end,
                               char* out, uint32_t cap)
{
    uint32_t n = 0;
    if (out && cap) out[0] = '\0';
    p = line_skip_spaces(data, p, end);
    while (p < end && data[p] != ' ' && data[p] != '\t' && data[p] != '\r') {
        if (out && cap && n + 1u < cap) out[n++] = (char)data[p];
        p++;
    }
    if (out && cap) out[n] = '\0';
    return p;
}

static void load_pin_line(const uint8_t* data, uint32_t start, uint32_t end)
{
    uint32_t p = start;
    uint32_t n = 0;
    uint32_t slot;
    uint32_t digits = 0;
    char kind[MEGACLIP_KIND_MAX + 1u];
    char label[MEGACLIP_LABEL_MAX + 1u];
    if (line_prefix_len(data, start, end, "ITEM PIN ", &p) ||
        line_prefix_len(data, start, end, "PIN ", &p)) {
        p = start + p;
    } else {
        return;
    }
    p = line_skip_spaces(data, p, end);
    while (p < end && data[p] >= '0' && data[p] <= '9') {
        n = n * 10u + (uint32_t)(data[p] - '0');
        digits++;
        p++;
    }
    if (!digits) return;
    if (parse_slot_number(n, &slot) != 0) return;
    p = line_read_word(data, p, end, kind, sizeof(kind));
    p = line_read_word(data, p, end, label, sizeof(label));
    p = line_skip_spaces(data, p, end);
    if (p + 1u < end && data[p] == ':' && data[p + 1u] == ':') p += 2u;
    p = line_skip_spaces(data, p, end);
    while (end > p && (data[end - 1u] == '\r' || data[end - 1u] == ' ' || data[end - 1u] == '\t')) end--;
    assign_pin_raw(slot, kind[0] ? kind : "text", label[0] ? label : "fixed", data + p, end - p);
}

static void megaclip_load_pins_doc(void)
{
    const FsFile* f = fs_open("megaclip.lardd");
    uint32_t line = 0;
    if (!f || !f->data) return;
    while (line < f->size) {
        uint32_t end = line;
        while (end < f->size && f->data[end] != '\n') end++;
        load_pin_line(f->data, line, end);
        line = end < f->size ? end + 1u : end;
    }
}

static int megaclip_save_pins_doc(void)
{
    FsWritableFile* f = fs_open_writable("megaclip.lardd");
    uint32_t pos = 0;
    if (!f) {
        s_last_error = 5;
        return -1;
    }
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, "LARDD 1\n");
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, "TITLE MegaClipboard\n");
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos,
               "TEXT Keyboard-first clipboard with fluid MegaClipboard history and fixed PinClip shortcuts.\n");
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos,
               "TEXT Ctrl+Y copies, Ctrl+P pastes latest, Ctrl+Space then 1..9/0 pulls history.\n");
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos,
               "TEXT Ctrl+Space then P then 1..9/0 pulls fixed slots that do not move when copying.\n");
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos,
               "TEXT Commands: megaclip status|list|mode|push|file|pull|write|clear and pinclip list|set|pull|write|clear|reload.\n");
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, "SECTION Fixed Slots\n");
    for (uint32_t i = 0; i < MEGACLIP_PIN_SLOTS; i++) {
        if (!s_pins[i].used) continue;
        doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, "ITEM PIN ");
        doc_append_u32(s_doc_buf, sizeof(s_doc_buf), &pos, pin_slot_number(i));
        doc_append_char(s_doc_buf, sizeof(s_doc_buf), &pos, ' ');
        doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, s_pins[i].kind);
        doc_append_char(s_doc_buf, sizeof(s_doc_buf), &pos, ' ');
        doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, s_pins[i].label);
        doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, " :: ");
        doc_append_clean_data(s_doc_buf, sizeof(s_doc_buf), &pos, s_pins[i].data, s_pins[i].size);
        doc_append_char(s_doc_buf, sizeof(s_doc_buf), &pos, '\n');
    }
    doc_append(s_doc_buf, sizeof(s_doc_buf), &pos, "END\n");
    if (fs_write(f, 0, (const uint8_t*)s_doc_buf, pos) != pos) {
        s_last_error = 6;
        return -1;
    }
    s_last_error = 0;
    return 0;
}

void megaclip_init(void)
{
    reset_runtime(1);
    megaclip_load_pins_doc();
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

int megaclip_pin_set(uint32_t slot, const char* kind, const char* label, const uint8_t* data, uint32_t size)
{
    if (slot >= MEGACLIP_PIN_SLOTS || (!data && size)) {
        s_last_error = 4;
        return -1;
    }
    assign_pin_raw(slot, kind, label, data, size);
    s_pin_sets++;
    return megaclip_save_pins_doc();
}

int megaclip_pin_set_text(uint32_t slot, const char* label, const char* text)
{
    return megaclip_pin_set(slot, "text", label, (const uint8_t*)text, slen(text));
}

int megaclip_pin_pull(uint32_t slot, megaclip_item_t* out)
{
    if (slot >= MEGACLIP_PIN_SLOTS || !out || !s_pins[slot].used) {
        s_last_error = 3;
        return -1;
    }
    *out = s_pins[slot];
    out->slot = slot;
    s_pin_pulls++;
    s_last_error = 0;
    return 0;
}

int megaclip_pin_clear(uint32_t slot)
{
    if (slot >= MEGACLIP_PIN_SLOTS) {
        s_last_error = 4;
        return -1;
    }
    clear_slot(&s_pins[slot]);
    s_pin_sets++;
    return megaclip_save_pins_doc();
}

uint32_t megaclip_pin_count(void)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < MEGACLIP_PIN_SLOTS; i++) if (s_pins[i].used) count++;
    return count;
}

int megaclip_pin_reload(void)
{
    for (uint32_t i = 0; i < MEGACLIP_PIN_SLOTS; i++) clear_slot(&s_pins[i]);
    megaclip_load_pins_doc();
    s_last_error = 0;
    return 0;
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
    out->pin_count = megaclip_pin_count();
    out->pin_sets = s_pin_sets;
    out->pin_pulls = s_pin_pulls;
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
    uint32_t old_pin_sets = s_pin_sets;
    uint32_t old_pin_pulls = s_pin_pulls;
    megaclip_item_t old_slots[MEGACLIP_SLOTS];
    megaclip_item_t old_pins[MEGACLIP_PIN_SLOTS];
    int ok = 1;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) old_slots[i] = s_slots[i];
    for (uint32_t i = 0; i < MEGACLIP_PIN_SLOTS; i++) old_pins[i] = s_pins[i];
    reset_runtime(1);
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
    assign_pin_raw(0, "text", "fixed-one", (const uint8_t*)"alpha", 5u);
    assign_pin_raw(9, "text", "fixed-zero", (const uint8_t*)"omega", 5u);
    megaclip_clear();
    if (ok && megaclip_count() != 0u) ok = 0;
    if (ok && megaclip_pin_count() != 2u) ok = 0;
    if (ok && megaclip_pin_pull(0, &item) != 0) ok = 0;
    if (ok && (item.size != 5u || item.data[0] != 'a')) ok = 0;
    if (ok && megaclip_pin_pull(9, &item) != 0) ok = 0;
    if (ok && (item.size != 5u || item.data[0] != 'o')) ok = 0;
    clear_slot(&s_pins[0]);
    if (ok && megaclip_pin_pull(0, &item) == 0) ok = 0;
    for (uint32_t i = 0; i < MEGACLIP_SLOTS; i++) s_slots[i] = old_slots[i];
    for (uint32_t i = 0; i < MEGACLIP_PIN_SLOTS; i++) s_pins[i] = old_pins[i];
    s_mode = old_mode;
    s_seq = old_seq;
    s_pushes = old_pushes;
    s_pulls = old_pulls;
    s_dropped = old_dropped;
    s_last_error = old_last_error;
    s_pin_sets = old_pin_sets;
    s_pin_pulls = old_pin_pulls;
    return ok ? 0 : -1;
}
