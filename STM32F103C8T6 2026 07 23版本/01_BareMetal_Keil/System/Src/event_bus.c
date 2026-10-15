#include "event_bus.h"
#include <stddef.h>

#define EVENT_QUEUE_LENGTH 16U

static app_event_t s_queue[EVENT_QUEUE_LENGTH];
static uint8_t s_head;
static uint8_t s_tail;
static uint8_t s_count;
static uint32_t s_dropped;

void event_bus_init(void)
{
    s_head = 0U;
    s_tail = 0U;
    s_count = 0U;
    s_dropped = 0U;
}

bool event_bus_post(app_event_t event)
{
    if (s_count >= EVENT_QUEUE_LENGTH) {
        ++s_dropped;
        return false;
    }
    s_queue[s_tail] = event;
    s_tail = (uint8_t)((s_tail + 1U) % EVENT_QUEUE_LENGTH);
    ++s_count;
    return true;
}

bool event_bus_pop(app_event_t *event)
{
    if ((event == NULL) || (s_count == 0U)) {
        return false;
    }
    *event = s_queue[s_head];
    s_head = (uint8_t)((s_head + 1U) % EVENT_QUEUE_LENGTH);
    --s_count;
    return true;
}

uint32_t event_bus_dropped(void)
{
    return s_dropped;
}

