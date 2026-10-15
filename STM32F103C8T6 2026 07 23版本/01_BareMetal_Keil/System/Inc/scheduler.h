#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*scheduler_task_fn)(void *context);

void scheduler_init(void);
bool scheduler_add(scheduler_task_fn function, void *context,
                   uint32_t period_ms, uint32_t first_run_ms);
void scheduler_run_ready(uint32_t now_ms);

#endif
