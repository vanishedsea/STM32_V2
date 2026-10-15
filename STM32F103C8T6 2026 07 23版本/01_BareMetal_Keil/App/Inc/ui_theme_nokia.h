#ifndef UI_THEME_NOKIA_H
#define UI_THEME_NOKIA_H

#include <stdbool.h>
#include <stdint.h>
#include "ui_service.h"

void ui_theme_nokia_render_menu(uint8_t selected, ui_theme_t theme,
                                bool buzzer_enabled, const char *fps_label);
bool ui_theme_nokia_render_animation_frame(uint8_t from_index,
                                            uint8_t to_index,
                                            uint8_t frame,
                                            ui_theme_t theme,
                                            bool buzzer_enabled,
                                            const char *fps_label);
void ui_theme_nokia_style_data_page(void);

#endif
