#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#include <stdbool.h>
#include "app_types.h"
#include "stm32f1xx_hal.h"

typedef enum {
    UI_INPUT_CLOCKWISE = 0,
    UI_INPUT_COUNTERCLOCKWISE,
    UI_INPUT_CLICK,
    UI_INPUT_LONG_PRESS
} ui_input_t;

typedef enum {
    UI_THEME_CLASSIC = 0,
    UI_THEME_NOKIA = 1
} ui_theme_t;

typedef struct {
    ui_theme_t theme;
    bool buzzer_enabled;
} ui_settings_t;

bool ui_service_init(I2C_HandleTypeDef *i2c, const char *edition);
bool ui_service_handle_input(ui_input_t input);
bool ui_service_show(const app_snapshot_t *snapshot, bool flash_ok, uint32_t log_count);
bool ui_service_fps_tick(uint32_t now_ms);
void ui_service_record_frame(void);
void ui_service_apply_settings(const ui_settings_t *settings);
bool ui_service_take_settings_changed(ui_settings_t *settings);
ui_theme_t ui_service_theme(void);
bool ui_service_buzzer_enabled(void);
bool ui_service_animation_active(void);

#endif
