#ifndef SHT30_H
#define SHT30_H

#include <stdbool.h>
#include "stm32f1xx_hal.h"

bool sht30_init(I2C_HandleTypeDef *i2c);
bool sht30_read(float *temperature_c, float *humidity_rh);

#endif

