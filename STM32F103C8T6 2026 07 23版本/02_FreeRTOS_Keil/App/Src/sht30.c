#include "sht30.h"
#include "app_config.h"

static I2C_HandleTypeDef *s_i2c;

static bool send_command(uint16_t command)
{
    uint8_t data[2] = {(uint8_t)(command >> 8U), (uint8_t)command};
    return HAL_I2C_Master_Transmit(s_i2c, APP_SHT30_ADDRESS << 1U, data, 2U,
                                  APP_I2C_TIMEOUT_MS) == HAL_OK;
}

static uint8_t sht_crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFFU;
    for (uint8_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1U) ^ 0x31U) : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

bool sht30_init(I2C_HandleTypeDef *i2c)
{
    s_i2c = i2c;
    if ((s_i2c == NULL) ||
        (HAL_I2C_IsDeviceReady(s_i2c, APP_SHT30_ADDRESS << 1U, 3U,
                               APP_I2C_TIMEOUT_MS) != HAL_OK)) {
        return false;
    }
    if (!send_command(0x30A2U)) {
        return false;
    }
    HAL_Delay(2U);
    /* Periodic mode: 1 measurement/s, high repeatability. */
    if (!send_command(0x2130U)) {
        return false;
    }
    HAL_Delay(20U);
    return true;
}

bool sht30_read(float *temperature_c, float *humidity_rh)
{
    uint8_t data[6];
    if ((temperature_c == NULL) || (humidity_rh == NULL) || (s_i2c == NULL) ||
        !send_command(0xE000U) ||
        (HAL_I2C_Master_Receive(s_i2c, APP_SHT30_ADDRESS << 1U, data, sizeof(data),
                                APP_I2C_TIMEOUT_MS) != HAL_OK)) {
        return false;
    }
    if ((sht_crc8(data, 2U) != data[2]) || (sht_crc8(&data[3], 2U) != data[5])) {
        return false;
    }
    uint16_t raw_t = (uint16_t)((data[0] << 8U) | data[1]);
    uint16_t raw_h = (uint16_t)((data[3] << 8U) | data[4]);
    *temperature_c = -45.0f + (175.0f * (float)raw_t / 65535.0f);
    *humidity_rh = 100.0f * (float)raw_h / 65535.0f;
    return true;
}

