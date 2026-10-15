#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

#define QUEUE_CAPACITY 4U

typedef enum {
    EVENT_NONE = 0,
    EVENT_CW,
    EVENT_CCW,
    EVENT_CLICK
} event_type_t;

typedef struct {
    event_type_t type;
    int32_t value;
} event_t;

typedef struct {
    event_t data[QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t size;
    uint32_t dropped;
    size_t high_water;
} event_queue_t;

static void queue_init(event_queue_t *queue)
{
    queue->head = 0U;
    queue->tail = 0U;
    queue->size = 0U;
    queue->dropped = 0U;
    queue->high_water = 0U;
}

static bool queue_push(event_queue_t *queue, event_t event)
{
    if (queue->size == QUEUE_CAPACITY) {
        ++queue->dropped;
        return false;
    }
    queue->data[queue->tail] = event;
    queue->tail = (queue->tail + 1U) % QUEUE_CAPACITY;
    ++queue->size;
    if (queue->size > queue->high_water) {
        queue->high_water = queue->size;
    }
    return true;
}

static bool queue_pop(event_queue_t *queue, event_t *event)
{
    if ((event == NULL) || (queue->size == 0U)) {
        return false;
    }
    *event = queue->data[queue->head];
    queue->head = (queue->head + 1U) % QUEUE_CAPACITY;
    --queue->size;
    return true;
}

int main(void)
{
    event_queue_t queue;
    event_t event;
    queue_init(&queue);

    assert(!queue_pop(&queue, &event));
    assert(queue_push(&queue, (event_t){EVENT_CW, 1}));
    assert(queue_push(&queue, (event_t){EVENT_CCW, -1}));
    assert(queue_push(&queue, (event_t){EVENT_CLICK, 0}));
    assert(queue_push(&queue, (event_t){EVENT_CW, 2}));
    assert(!queue_push(&queue, (event_t){EVENT_CW, 3}));
    assert(queue.dropped == 1U);

    assert(queue_pop(&queue, &event) && event.type == EVENT_CW && event.value == 1);
    assert(queue_pop(&queue, &event) && event.type == EVENT_CCW);

    /* Push twice after popping twice: tail wraps to the front. */
    assert(queue_push(&queue, (event_t){EVENT_CW, 4}));
    assert(queue_push(&queue, (event_t){EVENT_CW, 5}));

    const int32_t expected[] = {0, 2, 4, 5};
    for (size_t i = 0U; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        assert(queue_pop(&queue, &event));
        assert(event.value == expected[i]);
    }
    assert(!queue_pop(&queue, &event));
    assert(queue.high_water == QUEUE_CAPACITY);

    printf("ring buffer tests passed; dropped=%" PRIu32 ", high_water=%zu\n",
           queue.dropped, queue.high_water);
    return 0;
}
