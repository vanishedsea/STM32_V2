#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENCODER_EVENT_NONE = 0,
    ENCODER_EVENT_CLOCKWISE,
    ENCODER_EVENT_COUNTERCLOCKWISE,
    ENCODER_EVENT_CLICK,
    ENCODER_EVENT_LONG_PRESS
} encoder_event_t;

void encoder_init(void);
void encoder_update(uint32_t now_ms);
int16_t encoder_take_rotation(void);
bool encoder_get_event(encoder_event_t *event);
bool encoder_button_idle_high(void);
uint32_t encoder_dropped_events(void);

#endif
