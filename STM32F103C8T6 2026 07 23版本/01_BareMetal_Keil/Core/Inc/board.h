#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;

void board_init(void);
void board_led_set(bool on);
void board_led_toggle(void);
void board_uart_write(const uint8_t *data, size_t length);
uint8_t board_i2c_scan(uint8_t *addresses, uint8_t capacity);

#endif

