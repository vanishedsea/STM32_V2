#include "ui_theme_nokia.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "ssd1306.h"

#define NOKIA_CELL_WIDTH   61U
#define NOKIA_CELL_HEIGHT  25U

static const char *s_primary_labels[8] = {
    "DASH", "CLIMATE", "SYSTEM", "AIR LIGHT",
    "STYLE", "BEEP", "EXIT", "ABOUT"
};

static uint8_t centered_in_cell(uint8_t cell_x, const char *text)
{
    size_t width = strlen(text) * 6U;
    if (width != 0U) {
        --width;
    }
    if (width >= NOKIA_CELL_WIDTH) {
        return (uint8_t)(cell_x + 2U);
    }
    return (uint8_t)(cell_x + (NOKIA_CELL_WIDTH - width) / 2U);
}

static void cell_position(uint8_t local_index, uint8_t *x, uint8_t *y)
{
    static const uint8_t cell_x[4] = {2U, 65U, 65U, 2U};
    static const uint8_t cell_y[4] = {11U, 11U, 38U, 38U};
    uint8_t index = (uint8_t)(local_index & 3U);
    *x = cell_x[index];
    *y = cell_y[index];
}

static const char *secondary_label(uint8_t item, ui_theme_t theme,
                                   bool buzzer_enabled)
{
    static const char *labels[8] = {
        "DATA", "TEMP HUM", "STATUS", "PRESS LUX",
        "", "", "BACK", "STM32"
    };
    if (item == 4U) {
        return (theme == UI_THEME_NOKIA) ? "NOKIA" : "CLASSIC";
    }
    if (item == 5U) {
        return buzzer_enabled ? "ON" : "OFF";
    }
    return labels[item];
}

static void render_base(uint8_t selected, ui_theme_t theme, bool buzzer_enabled,
                        const char *fps_label)
{
    char header[12];
    uint8_t page = (uint8_t)(selected / 4U);
    uint8_t first_item = (uint8_t)(page * 4U);

    ssd1306_clear();
    (void)snprintf(header, sizeof(header), "MENU %u/2", (unsigned int)(page + 1U));
    ssd1306_draw_text(2U, 1U, header);
    ssd1306_draw_text(68U, 1U, fps_label);
    ssd1306_draw_text(91U, 1U, "SELECT");
    ssd1306_draw_hline(0U, 9U, SSD1306_WIDTH);

    for (uint8_t local = 0U; local < 4U; ++local) {
        uint8_t x;
        uint8_t y;
        uint8_t item = (uint8_t)(first_item + local);
        const char *secondary = secondary_label(item, theme, buzzer_enabled);
        cell_position(local, &x, &y);
        ssd1306_draw_rect(x, y, NOKIA_CELL_WIDTH, NOKIA_CELL_HEIGHT);
        ssd1306_draw_text(centered_in_cell(x, s_primary_labels[item]),
                          (uint8_t)(y + 4U), s_primary_labels[item]);
        ssd1306_draw_text(centered_in_cell(x, secondary),
                          (uint8_t)(y + 13U), secondary);
    }
}

void ui_theme_nokia_render_menu(uint8_t selected, ui_theme_t theme,
                                bool buzzer_enabled, const char *fps_label)
{
    uint8_t x;
    uint8_t y;
    render_base(selected, theme, buzzer_enabled, fps_label);
    cell_position(selected, &x, &y);
    ssd1306_invert_rect(x, y, NOKIA_CELL_WIDTH, NOKIA_CELL_HEIGHT);
}

bool ui_theme_nokia_render_animation_frame(uint8_t from_index,
                                            uint8_t to_index,
                                            uint8_t frame,
                                            ui_theme_t theme,
                                            bool buzzer_enabled,
                                            const char *fps_label)
{
    uint8_t from_x;
    uint8_t from_y;
    uint8_t to_x;
    uint8_t to_y;
    const uint16_t denominator = APP_UI_ANIMATION_FRAMES * APP_UI_ANIMATION_FRAMES;

    if (frame == 0U) {
        frame = 1U;
    } else if (frame > APP_UI_ANIMATION_FRAMES) {
        frame = APP_UI_ANIMATION_FRAMES;
    }
    cell_position(from_index, &from_x, &from_y);
    cell_position(to_index, &to_x, &to_y);
    uint16_t remaining = (uint16_t)(APP_UI_ANIMATION_FRAMES - frame);
    uint16_t eased = (uint16_t)(denominator - remaining * remaining);
    int16_t x = (int16_t)from_x +
                ((int16_t)to_x - (int16_t)from_x) * (int16_t)eased /
                (int16_t)denominator;
    int16_t y = (int16_t)from_y +
                ((int16_t)to_y - (int16_t)from_y) * (int16_t)eased /
                (int16_t)denominator;
    render_base(to_index, theme, buzzer_enabled, fps_label);
    ssd1306_invert_rect((uint8_t)x, (uint8_t)y,
                        NOKIA_CELL_WIDTH, NOKIA_CELL_HEIGHT);
    return ssd1306_update();
}

void ui_theme_nokia_style_data_page(void)
{
    ssd1306_invert_rect(0U, 0U, SSD1306_WIDTH, 10U);
}
