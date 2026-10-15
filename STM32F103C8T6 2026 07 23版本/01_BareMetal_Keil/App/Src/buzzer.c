#include "buzzer.h"
#include "app_config.h"
#include "stm32f1xx_hal.h"

static bool s_pulse_active;
static uint32_t s_off_deadline_ms;

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void write_output(bool on)
{
    GPIO_PinState state;
    if (APP_BUZZER_ACTIVE_HIGH) {
        state = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    } else {
        state = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, state);
}

void buzzer_set(bool on)
{
    s_pulse_active = false;
    write_output(on);
}

void buzzer_pulse_start(uint32_t duration_ms, uint32_t now_ms)
{
    if (duration_ms == 0U) {
        buzzer_set(false);
        return;
    }
    if (duration_ms > APP_BUZZER_MAX_ON_MS) {
        duration_ms = APP_BUZZER_MAX_ON_MS;
    }
    s_off_deadline_ms = now_ms + duration_ms;
    s_pulse_active = true;
    write_output(true);
}

void buzzer_update(uint32_t now_ms)
{
    if (s_pulse_active && time_reached(now_ms, s_off_deadline_ms)) {
        s_pulse_active = false;
        write_output(false);
    }
}

bool buzzer_active(void)
{
    return s_pulse_active;
}
