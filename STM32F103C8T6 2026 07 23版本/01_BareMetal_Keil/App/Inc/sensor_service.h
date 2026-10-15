#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include <stdint.h>
#include "app_types.h"
#include "stm32f1xx_hal.h"

uint8_t sensor_service_init(I2C_HandleTypeDef *i2c);
uint8_t sensor_service_read(app_snapshot_t *snapshot, uint32_t sequence);

#endif

