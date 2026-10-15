#ifndef BMP280_H
#define BMP280_H

#include <stdbool.h>
#include "stm32f1xx_hal.h"

bool bmp280_init(I2C_HandleTypeDef *i2c);
bool bmp280_read(float *temperature_c, float *pressure_pa);

#endif

