#ifndef W25Q64_H
#define W25Q64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

bool w25q64_init(SPI_HandleTypeDef *spi, uint32_t *jedec_id);
bool w25q64_read(uint32_t address, void *data, size_t length);
bool w25q64_is_ready(bool *ready);
bool w25q64_program_begin(uint32_t address, const void *data, size_t length);
bool w25q64_erase_sector_begin(uint32_t address);
bool w25q64_program(uint32_t address, const void *data, size_t length);
bool w25q64_erase_sector(uint32_t address);

#endif
