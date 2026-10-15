#include "encoder.h"
#include "app_config.h"
#include "stm32f1xx_hal.h"

#define ENCODER_EVENT_QUEUE_LENGTH 8U

static const int8_t s_transition_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static encoder_event_t s_events[ENCODER_EVENT_QUEUE_LENGTH];
static uint8_t s_event_head;
static uint8_t s_event_tail;
static uint8_t s_event_count;
static volatile uint8_t s_previous_ab;
static volatile int8_t s_quadrature_accumulator;
static volatile int16_t s_pending_detents;
static volatile bool s_rotation_irq_ready;
static uint32_t s_dropped_events;
static bool s_button_raw;
static bool s_button_stable;
static bool s_long_press_sent;
static GPIO_PinState s_button_idle_level;
static uint32_t s_button_change_ms;
static uint32_t s_button_press_ms;

static bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms)
{
    return (uint32_t)(now_ms - since_ms) >= duration_ms;
}

static uint8_t read_ab(void)
{
    uint8_t a = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET) ? 1U : 0U;
    uint8_t b = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_SET) ? 1U : 0U;
    return (uint8_t)((a << 1U) | b);
}

static bool read_button_pressed(void)
{
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_RESET;
}

static void push_event(encoder_event_t event)
{
    if (event == ENCODER_EVENT_NONE) {
        return;
    }
    if (s_event_count >= ENCODER_EVENT_QUEUE_LENGTH) {
        ++s_dropped_events;
        return;
    }
    s_events[s_event_tail] = event;
    s_event_tail = (uint8_t)((s_event_tail + 1U) % ENCODER_EVENT_QUEUE_LENGTH);
    ++s_event_count;
}

static void capture_rotation_edge(void)
{
    if (!s_rotation_irq_ready) {
        return;
    }
    uint8_t current_ab = read_ab();
    if (current_ab == s_previous_ab) {
        return;
    }
    uint8_t transition = (uint8_t)((s_previous_ab << 2U) | current_ab);
    s_quadrature_accumulator = (int8_t)(s_quadrature_accumulator +
                                        s_transition_table[transition]);
    s_previous_ab = current_ab;
    if (s_quadrature_accumulator >= 4) {
        s_quadrature_accumulator = 0;
        if (s_pending_detents > INT16_MIN) {
            --s_pending_detents;
        }
    } else if (s_quadrature_accumulator <= -4) {
        s_quadrature_accumulator = 0;
        if (s_pending_detents < INT16_MAX) {
            ++s_pending_detents;
        }
    }
}

void encoder_init(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_event_head = 0U;
    s_event_tail = 0U;
    s_event_count = 0U;
    s_previous_ab = read_ab();
    s_quadrature_accumulator = 0;
    s_pending_detents = 0;
    s_rotation_irq_ready = true;
    s_dropped_events = 0U;
    if (primask == 0U) {
        __enable_irq();
    }
    /* The separate PB15 button is deterministic active-low with an internal
       pull-up; it no longer depends on the EC11 module's C/SW pin. */
    s_button_idle_level = GPIO_PIN_SET;
    s_button_raw = false;
    s_button_stable = false;
    s_long_press_sent = false;
    s_button_change_ms = HAL_GetTick();
    s_button_press_ms = s_button_change_ms;
}

void encoder_update(uint32_t now_ms)
{
    bool pressed = read_button_pressed();
    if (pressed != s_button_raw) {
        s_button_raw = pressed;
        s_button_change_ms = now_ms;
    }
    if ((s_button_stable != s_button_raw) &&
        elapsed(now_ms, s_button_change_ms, APP_ENCODER_BUTTON_DEBOUNCE_MS)) {
        s_button_stable = s_button_raw;
        if (s_button_stable) {
            s_button_press_ms = now_ms;
            s_long_press_sent = false;
        } else if (!s_long_press_sent) {
            push_event(ENCODER_EVENT_CLICK);
        }
    }
    if (s_button_stable && !s_long_press_sent &&
        elapsed(now_ms, s_button_press_ms, APP_ENCODER_LONG_PRESS_MS)) {
        s_long_press_sent = true;
        push_event(ENCODER_EVENT_LONG_PRESS);
    }
}

int16_t encoder_take_rotation(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int16_t rotation = s_pending_detents;
    s_pending_detents = 0;
    if (primask == 0U) {
        __enable_irq();
    }
    if (APP_ENCODER_REVERSE && (rotation != INT16_MIN)) {
        rotation = (int16_t)-rotation;
    }
    return rotation;
}

bool encoder_get_event(encoder_event_t *event)
{
    if ((event == NULL) || (s_event_count == 0U)) {
        return false;
    }
    *event = s_events[s_event_head];
    s_event_head = (uint8_t)((s_event_head + 1U) % ENCODER_EVENT_QUEUE_LENGTH);
    --s_event_count;
    return true;
}

bool encoder_button_idle_high(void)
{
    return s_button_idle_level == GPIO_PIN_SET;
}

uint32_t encoder_dropped_events(void)
{
    return s_dropped_events;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if ((GPIO_Pin == GPIO_PIN_12) || (GPIO_Pin == GPIO_PIN_13)) {
        capture_rotation_edge();
    }
}
