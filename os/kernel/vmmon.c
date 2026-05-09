#include "vmmon.h"

static const uint32_t s_budgets[VMMON_COUNT] = {
    1000000u,  /* BOSL */
    500000u,   /* LIL */
    1000000u,  /* GASM */
    100000u,   /* Lafillo VM */
    1000000u   /* OSVM */
};

static vmmon_entry_t s_entries[VMMON_COUNT] = {
    { "bosl", 0, 0, 0, 0, 0, 0 },
    { "lil", 0, 0, 0, 0, 0, 0 },
    { "gasm", 0, 0, 0, 0, 0, 0 },
    { "lafillo", 0, 0, 0, 0, 0, 0 },
    { "osvm", 0, 0, 0, 0, 0, 0 }
};

static int valid_id(uint32_t id)
{
    return id < VMMON_COUNT;
}

const char* vmmon_name(uint32_t id)
{
    return valid_id(id) ? s_entries[id].name : "unknown";
}

uint32_t vmmon_budget(uint32_t id)
{
    return valid_id(id) ? s_budgets[id] : 100000u;
}

void vmmon_record(uint32_t id, uint32_t steps, int rc)
{
    if (!valid_id(id)) {
        return;
    }
    vmmon_entry_t* e = &s_entries[id];
    e->runs++;
    e->last_steps = steps;
    e->last_rc = rc;
    if (steps > e->max_steps) {
        e->max_steps = steps;
    }
    if (rc != 0) {
        e->failures++;
        if (steps >= vmmon_budget(id)) {
            e->budget_hits++;
        }
    }
}

void vmmon_reset(void)
{
    for (uint32_t i = 0; i < VMMON_COUNT; i++) {
        s_entries[i].runs = 0;
        s_entries[i].failures = 0;
        s_entries[i].budget_hits = 0;
        s_entries[i].last_steps = 0;
        s_entries[i].max_steps = 0;
        s_entries[i].last_rc = 0;
    }
}

int vmmon_info(uint32_t id, vmmon_entry_t* out)
{
    if (!valid_id(id) || !out) {
        return -1;
    }
    *out = s_entries[id];
    return 0;
}

int vmmon_selftest(void)
{
    if (VMMON_COUNT != 5) {
        return -1;
    }
    for (uint32_t i = 0; i < VMMON_COUNT; i++) {
        if (!s_entries[i].name[0] || vmmon_budget(i) < 1000u) {
            return -2;
        }
    }
    return 0;
}
