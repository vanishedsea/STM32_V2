#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

typedef enum {
    OP_IDLE = 0,
    OP_WAIT_ERASE,
    OP_WAIT_PROGRAM,
    OP_DONE,
    OP_ERROR
} operation_state_t;

typedef struct {
    operation_state_t state;
    uint32_t deadline_ms;
    uint32_t hardware_ready_ms;
    bool request;
} operation_t;

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void operation_request(operation_t *op)
{
    if (op->state == OP_IDLE) {
        op->request = true;
    }
}

static void operation_process(operation_t *op, uint32_t now)
{
    switch (op->state) {
    case OP_IDLE:
        if (op->request) {
            op->request = false;
            op->hardware_ready_ms = now + 12U;
            op->deadline_ms = now + 30U;
            op->state = OP_WAIT_ERASE;
        }
        break;

    case OP_WAIT_ERASE:
        if (time_reached(now, op->hardware_ready_ms)) {
            op->hardware_ready_ms = now + 4U;
            op->deadline_ms = now + 10U;
            op->state = OP_WAIT_PROGRAM;
        } else if (time_reached(now, op->deadline_ms)) {
            op->state = OP_ERROR;
        }
        break;

    case OP_WAIT_PROGRAM:
        if (time_reached(now, op->hardware_ready_ms)) {
            op->state = OP_DONE;
        } else if (time_reached(now, op->deadline_ms)) {
            op->state = OP_ERROR;
        }
        break;

    case OP_DONE:
        op->state = OP_IDLE;
        break;

    case OP_ERROR:
        break;
    }
}

int main(void)
{
    operation_t op = {OP_IDLE, 0U, 0U, false};
    operation_request(&op);

    for (uint32_t now = 0U; now <= 25U; ++now) {
        operation_state_t before = op.state;
        operation_process(&op, now);
        if (op.state != before) {
            printf("t=%" PRIu32 " state %d -> %d\n",
                   now, (int)before, (int)op.state);
        }
    }

    assert(op.state == OP_IDLE);
    puts("state machine completed without blocking the loop");
    return 0;
}
