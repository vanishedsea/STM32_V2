#include "bh1750.h"
#include "app_config.h"

static I2C_HandleTypeDef *s_i2c;

static bool send_command(uint8_t command)
{
    return HAL_I2C_Master_Transmit(s_i2c, APP_BH1750_ADDRESS << 1U,
                                   &command, 1U, APP_I2C_TIMEOUT_MS) == HAL_OK;
}

bool bh1750_init(I2C_HandleTypeDef *i2c)
{
    s_i2c = i2c;
    if ((s_i2c == NULL) ||
        (HAL_I2C_IsDeviceReady(s_i2c, APP_BH1750_ADDRESS << 1U, 3U,
                               APP_I2C_TIMEOUT_MS) != HAL_OK)) {
        return false;
    }
    if (!send_command(0x01U) || !send_command(0x07U) || !send_command(0x10U)) {
        return false;
    }
    HAL_Delay(180U);
    return true;
}

bool bh1750_read(float *illuminance_lux)
{
    uint8_t data[2];
    if ((illuminance_lux == NULL) || (s_i2c == NULL) ||
        (HAL_I2C_Master_Receive(s_i2c, APP_BH1750_ADDRESS << 1U, data, 2U,
                                APP_I2C_TIMEOUT_MS) != HAL_OK)) {
        return false;
    }
    uint16_t raw = (uint16_t)((data[0] << 8U) | data[1]);
    *illuminance_lux = (float)raw / 1.2f;
    return true;
}

