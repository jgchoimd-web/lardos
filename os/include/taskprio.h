#pragma once

#include <stdint.h>

#define TASKPRIO_MAX_TASKS 8u
#define TASKPRIO_HISTORY_MAX 16u
#define TASKPRIO_CMD_MAX 256u
#define TASKPRIO_NAME_MAX 31u
#define TASKPRIO_ACTOR_MAX 15u
#define TASKPRIO_ACTION_MAX 31u
#define TASKPRIO_MIN 0
#define TASKPRIO_MAX 10
#define TASKPRIO_OS_LEVEL 10
#define TASKPRIO_DEFAULT 5

typedef struct {
    uint32_t id;
    char name[TASKPRIO_NAME_MAX + 1u];
    char command[TASKPRIO_CMD_MAX];
    int32_t priority;
    uint32_t wait_ticks;
    uint32_t runs;
    uint32_t paused;
} taskprio_task_t;

typedef struct {
    uint32_t queued;
    uint32_t completed;
    uint32_t next_id;
    uint32_t last_id;
    uint32_t paused;
    uint32_t runnable;
    uint32_t os_urgent;
    int32_t default_priority;
} taskprio_info_t;

typedef struct {
    uint32_t seq;
    uint32_t id;
    char name[TASKPRIO_NAME_MAX + 1u];
    int32_t old_priority;
    int32_t new_priority;
    char actor[TASKPRIO_ACTOR_MAX + 1u];
    char action[TASKPRIO_ACTION_MAX + 1u];
} taskprio_history_entry_t;

void taskprio_init(void);
int taskprio_enqueue(const char* name, const char* command, int32_t priority, uint32_t* out_id);
int taskprio_enqueue_os(const char* name, const char* command, uint32_t* out_id);
int taskprio_dequeue_next(taskprio_task_t* out);
uint32_t taskprio_count(void);
int taskprio_at(uint32_t idx, taskprio_task_t* out);
int taskprio_set_priority(uint32_t id, int32_t priority);
int taskprio_adjust_priority(uint32_t id, int32_t delta);
int taskprio_grant_urgent_priority(uint32_t id);
int taskprio_grant_os_priority(uint32_t id);
int taskprio_pause(uint32_t id, int pause);
int taskprio_remove(uint32_t id);
void taskprio_set_default(int32_t priority);
int32_t taskprio_default_priority(void);
void taskprio_info(taskprio_info_t* out);
uint32_t taskprio_history_count(void);
int taskprio_history_at(uint32_t idx, taskprio_history_entry_t* out);
void taskprio_history_clear(void);
int taskprio_selftest(void);
