#include "taskprio.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    taskprio_task_t tasks[TASKPRIO_MAX_TASKS];
    uint32_t count;
    uint32_t completed;
    uint32_t next_id;
    uint32_t last_id;
    int32_t default_priority;
} taskprio_state_t;

static taskprio_state_t s_taskprio;

static int32_t clamp_user_priority(int32_t p)
{
    if (p < TASKPRIO_MIN) return TASKPRIO_MIN;
    if (p > TASKPRIO_MAX) return TASKPRIO_MAX;
    return p;
}

static int is_os_priority(const taskprio_task_t* t)
{
    return t && t->priority == TASKPRIO_OS_LEVEL;
}

static uint32_t slen(const char* s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n] && n < cap) n++;
    return n;
}

static void scopy(char* dst, uint32_t cap, const char* src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_name_from_command(char* dst, uint32_t cap, const char* command)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!command) command = "";
    while (*command == ' ' || *command == '\t') command++;
    while (command[i] && command[i] != ' ' && command[i] != '\t' && i + 1u < cap) {
        dst[i] = command[i];
        i++;
    }
    if (i == 0) {
        scopy(dst, cap, "task");
    } else {
        dst[i] = '\0';
    }
}

static int find_index(uint32_t id)
{
    for (uint32_t i = 0; i < s_taskprio.count; i++) {
        if (s_taskprio.tasks[i].id == id) return (int)i;
    }
    return -1;
}

static void remove_at(uint32_t idx)
{
    if (idx >= s_taskprio.count) return;
    for (uint32_t i = idx; i + 1u < s_taskprio.count; i++) {
        s_taskprio.tasks[i] = s_taskprio.tasks[i + 1u];
    }
    s_taskprio.count--;
}

void taskprio_init(void)
{
    for (uint32_t i = 0; i < sizeof(s_taskprio); i++) ((uint8_t*)&s_taskprio)[i] = 0;
    s_taskprio.next_id = 1;
    s_taskprio.default_priority = TASKPRIO_DEFAULT;
}

static int enqueue_with_priority(const char* name, const char* command, int32_t priority, uint32_t* out_id)
{
    if (!command || slen(command, TASKPRIO_CMD_MAX) == 0) return -1;
    if (s_taskprio.count >= TASKPRIO_MAX_TASKS) return -2;
    taskprio_task_t* t = &s_taskprio.tasks[s_taskprio.count++];
    t->id = s_taskprio.next_id++;
    if (s_taskprio.next_id == 0) s_taskprio.next_id = 1;
    if (name && name[0]) scopy(t->name, sizeof(t->name), name);
    else copy_name_from_command(t->name, sizeof(t->name), command);
    scopy(t->command, sizeof(t->command), command);
    t->priority = priority;
    t->wait_ticks = 0;
    t->runs = 0;
    t->paused = 0;
    s_taskprio.last_id = t->id;
    if (out_id) *out_id = t->id;
    return 0;
}

int taskprio_enqueue(const char* name, const char* command, int32_t priority, uint32_t* out_id)
{
    return enqueue_with_priority(name, command, clamp_user_priority(priority), out_id);
}

int taskprio_enqueue_os(const char* name, const char* command, uint32_t* out_id)
{
    return enqueue_with_priority(name && name[0] ? name : "os", command, TASKPRIO_OS_LEVEL, out_id);
}

int taskprio_dequeue_next(taskprio_task_t* out)
{
    if (s_taskprio.count == 0) return 0;
    uint32_t best = 0xFFFFFFFFu;
    int32_t best_score = -1;
    for (uint32_t i = 0; i < s_taskprio.count; i++) {
        if (!s_taskprio.tasks[i].paused && is_os_priority(&s_taskprio.tasks[i])) {
            best = i;
            break;
        }
    }
    if (best == 0xFFFFFFFFu) {
        for (uint32_t i = 0; i < s_taskprio.count; i++) {
            uint32_t age = s_taskprio.tasks[i].wait_ticks;
            if (s_taskprio.tasks[i].paused || is_os_priority(&s_taskprio.tasks[i])) continue;
            if (age > 15u) age = 15u;
            int32_t score = s_taskprio.tasks[i].priority * 16 + (int32_t)age;
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
    }
    if (best == 0xFFFFFFFFu) {
        for (uint32_t i = 0; i < s_taskprio.count; i++) {
            if (!is_os_priority(&s_taskprio.tasks[i]) && s_taskprio.tasks[i].wait_ticks < 255u) {
                s_taskprio.tasks[i].wait_ticks++;
            }
        }
        return 0;
    }
    if (out) {
        *out = s_taskprio.tasks[best];
        out->runs++;
    }
    s_taskprio.completed++;
    remove_at(best);
    for (uint32_t i = 0; i < s_taskprio.count; i++) {
        if (!is_os_priority(&s_taskprio.tasks[i]) && s_taskprio.tasks[i].wait_ticks < 255u) {
            s_taskprio.tasks[i].wait_ticks++;
        }
    }
    return 1;
}

uint32_t taskprio_count(void)
{
    return s_taskprio.count;
}

int taskprio_at(uint32_t idx, taskprio_task_t* out)
{
    if (!out || idx >= s_taskprio.count) return -1;
    *out = s_taskprio.tasks[idx];
    return 0;
}

int taskprio_set_priority(uint32_t id, int32_t priority)
{
    int idx = find_index(id);
    if (idx < 0) return -1;
    if (is_os_priority(&s_taskprio.tasks[(uint32_t)idx])) return -2;
    s_taskprio.tasks[(uint32_t)idx].priority = clamp_user_priority(priority);
    s_taskprio.tasks[(uint32_t)idx].wait_ticks = 0;
    return 0;
}

int taskprio_adjust_priority(uint32_t id, int32_t delta)
{
    int idx = find_index(id);
    if (idx < 0) return -1;
    taskprio_task_t* t = &s_taskprio.tasks[(uint32_t)idx];
    if (is_os_priority(t)) return -2;
    t->priority = clamp_user_priority(t->priority + delta);
    t->wait_ticks = 0;
    return 0;
}

int taskprio_grant_os_priority(uint32_t id)
{
    int idx = find_index(id);
    if (idx < 0) return -1;
    s_taskprio.tasks[(uint32_t)idx].priority = TASKPRIO_OS_LEVEL;
    s_taskprio.tasks[(uint32_t)idx].wait_ticks = 0;
    s_taskprio.tasks[(uint32_t)idx].paused = 0;
    return 0;
}

int taskprio_pause(uint32_t id, int pause)
{
    int idx = find_index(id);
    if (idx < 0) return -1;
    if (is_os_priority(&s_taskprio.tasks[(uint32_t)idx])) return -2;
    s_taskprio.tasks[(uint32_t)idx].paused = pause ? 1u : 0u;
    return 0;
}

int taskprio_remove(uint32_t id)
{
    int idx = find_index(id);
    if (idx < 0) return -1;
    if (is_os_priority(&s_taskprio.tasks[(uint32_t)idx])) return -2;
    remove_at((uint32_t)idx);
    return 0;
}

void taskprio_set_default(int32_t priority)
{
    s_taskprio.default_priority = clamp_user_priority(priority);
}

int32_t taskprio_default_priority(void)
{
    return s_taskprio.default_priority;
}

void taskprio_info(taskprio_info_t* out)
{
    if (!out) return;
    out->queued = s_taskprio.count;
    out->completed = s_taskprio.completed;
    out->next_id = s_taskprio.next_id;
    out->last_id = s_taskprio.last_id;
    out->paused = 0;
    out->runnable = 0;
    out->os_urgent = 0;
    for (uint32_t i = 0; i < s_taskprio.count; i++) {
        if (is_os_priority(&s_taskprio.tasks[i])) out->os_urgent++;
        if (s_taskprio.tasks[i].paused) out->paused++;
        else out->runnable++;
    }
    out->default_priority = s_taskprio.default_priority;
}

int taskprio_selftest(void)
{
    taskprio_state_t saved = s_taskprio;
    taskprio_task_t t;
    uint32_t low_id = 0;
    uint32_t high_id = 0;
    uint32_t user_id = 0;
    uint32_t os_id = 0;
    taskprio_info_t info;
    taskprio_init();
    if (taskprio_enqueue("low", "echo low", 1, &low_id) != 0) {
        s_taskprio = saved;
        return -1;
    }
    if (taskprio_enqueue("high", "echo high", 8, &high_id) != 0) {
        s_taskprio = saved;
        return -2;
    }
    if (taskprio_set_priority(low_id, 9) != 0) {
        s_taskprio = saved;
        return -3;
    }
    if (taskprio_pause(high_id, 1) != 0) {
        s_taskprio = saved;
        return -4;
    }
    if (taskprio_dequeue_next(&t) != 1 || t.id != low_id || t.priority != 9) {
        s_taskprio = saved;
        return -5;
    }
    if (taskprio_pause(high_id, 0) != 0 || taskprio_adjust_priority(high_id, -3) != 0) {
        s_taskprio = saved;
        return -6;
    }
    if (taskprio_remove(high_id) != 0 || taskprio_count() != 0) {
        s_taskprio = saved;
        return -7;
    }
    taskprio_set_default(7);
    if (taskprio_default_priority() != 7) {
        s_taskprio = saved;
        return -8;
    }
    if (taskprio_enqueue("user", "echo user", 10, &user_id) != 0) {
        s_taskprio = saved;
        return -9;
    }
    if (taskprio_at(0, &t) != 0 || t.priority != TASKPRIO_MAX) {
        s_taskprio = saved;
        return -10;
    }
    s_taskprio.tasks[0].wait_ticks = 255u;
    if (taskprio_enqueue_os("panic", "crashlog show", &os_id) != 0) {
        s_taskprio = saved;
        return -11;
    }
    taskprio_info(&info);
    if (info.os_urgent != 1u) {
        s_taskprio = saved;
        return -12;
    }
    if (taskprio_dequeue_next(&t) != 1 || t.id != os_id || t.priority != TASKPRIO_OS_LEVEL) {
        s_taskprio = saved;
        return -13;
    }
    if (taskprio_grant_os_priority(user_id) != 0) {
        s_taskprio = saved;
        return -14;
    }
    if (taskprio_dequeue_next(&t) != 1 || t.id != user_id || t.priority != TASKPRIO_OS_LEVEL) {
        s_taskprio = saved;
        return -15;
    }
    s_taskprio = saved;
    return 0;
}
