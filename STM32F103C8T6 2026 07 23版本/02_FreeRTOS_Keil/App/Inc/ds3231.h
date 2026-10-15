#ifndef DS3231_H
#define DS3231_H

#include <stdbool.h>
#include "app_types.h"
#include "stm32f1xx_hal.h"

bool ds3231_init(I2C_HandleTypeDef *i2c);
bool ds3231_read(app_datetime_t *datetime);
bool ds3231_set(const app_datetime_t *datetime);
bool ds3231_lost_power(bool *lost_power);
bool ds3231_set_build_time(void);

#endif

