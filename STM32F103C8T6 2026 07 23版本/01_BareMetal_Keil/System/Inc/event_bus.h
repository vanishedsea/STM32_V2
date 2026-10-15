#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_NEW_SNAPSHOT,
    APP_EVENT_STORAGE_ERROR,
    APP_EVENT_ENCODER_ROTATE,
    APP_EVENT_ENCODER_CLOCKWISE,
    APP_EVENT_ENCODER_COUNTERCLOCKWISE,
    APP_EVENT_ENCODER_CLICK,
    APP_EVENT_ENCODER_LONG_PRESS
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint32_t value;
} app_event_t;

void event_bus_init(void);
bool event_bus_post(app_event_t event);
bool event_bus_pop(app_event_t *event);
uint32_t event_bus_dropped(void);

#endif
