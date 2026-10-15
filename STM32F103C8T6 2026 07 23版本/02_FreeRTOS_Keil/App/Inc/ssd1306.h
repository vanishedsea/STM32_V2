#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

#define SSD1306_WIDTH  128U
#define SSD1306_HEIGHT 64U

typedef struct {
    uint32_t update_calls;
    uint32_t transmitted_runs;
    uint32_t pixel_bytes;
} ssd1306_stats_t;

bool ssd1306_init(I2C_HandleTypeDef *i2c);
void ssd1306_clear(void);
void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on);
void ssd1306_draw_hline(uint8_t x, uint8_t y, uint8_t width);
void ssd1306_draw_vline(uint8_t x, uint8_t y, uint8_t height);
void ssd1306_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
void ssd1306_invert_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
void ssd1306_draw_char(uint8_t x, uint8_t y, char character);
void ssd1306_draw_text(uint8_t x, uint8_t y, const char *text);
void ssd1306_draw_text_scaled(uint8_t x, uint8_t y, const char *text, uint8_t scale);
bool ssd1306_update(void);
uint8_t ssd1306_address(void);
void ssd1306_get_stats(ssd1306_stats_t *stats);

#endif
