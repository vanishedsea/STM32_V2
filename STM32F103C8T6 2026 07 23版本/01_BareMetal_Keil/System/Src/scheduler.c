#include "scheduler.h"
#include <stddef.h>

#define SCHEDULER_MAX_TASKS 8U

typedef struct {
    scheduler_task_fn function;
    void *context;
    uint32_t period_ms;
    uint32_t next_run_ms;
    bool used;
} scheduler_entry_t;

static scheduler_entry_t s_tasks[SCHEDULER_MAX_TASKS];

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

void scheduler_init(void)
{
    for (uint8_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        s_tasks[i].used = false;
    }
}

bool scheduler_add(scheduler_task_fn function, void *context,
                   uint32_t period_ms, uint32_t first_run_ms)
{
    if ((function == NULL) || (period_ms == 0U)) {
        return false;
    }
    for (uint8_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        if (!s_tasks[i].used) {
            s_tasks[i].function = function;
            s_tasks[i].context = context;
            s_tasks[i].period_ms = period_ms;
            s_tasks[i].next_run_ms = first_run_ms;
            s_tasks[i].used = true;
            return true;
        }
    }
    return false;
}

void scheduler_run_ready(uint32_t now_ms)
{
    for (uint8_t i = 0U; i < SCHEDULER_MAX_TASKS; ++i) {
        scheduler_entry_t *task = &s_tasks[i];
        if (!task->used || !time_reached(now_ms, task->next_run_ms)) {
            continue;
        }
        uint32_t old_deadline = task->next_run_ms;
        task->next_run_ms += task->period_ms;
        if (time_reached(now_ms, task->next_run_ms) &&
            ((now_ms - old_deadline) > task->period_ms)) {
            task->next_run_ms = now_ms + task->period_ms;
        }
        task->function(task->context);
    }
}
