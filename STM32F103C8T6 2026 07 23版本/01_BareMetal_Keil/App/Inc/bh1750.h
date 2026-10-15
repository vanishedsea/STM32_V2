#ifndef BH1750_H
#define BH1750_H

#include <stdbool.h>
#include "stm32f1xx_hal.h"

bool bh1750_init(I2C_HandleTypeDef *i2c);
bool bh1750_read(float *illuminance_lux);

#endif

