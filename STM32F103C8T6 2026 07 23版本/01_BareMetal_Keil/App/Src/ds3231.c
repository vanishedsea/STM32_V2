#include "ds3231.h"
#include <string.h>
#include "app_config.h"

#define DS3231_REG_TIME       0x00U
#define DS3231_REG_CONTROL    0x0EU
#define DS3231_REG_STATUS     0x0FU

static I2C_HandleTypeDef *s_i2c;

static uint8_t bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
}

static uint8_t bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static bool read_regs(uint8_t reg, uint8_t *data, uint16_t length)
{
    return (s_i2c != NULL) &&
           (HAL_I2C_Mem_Read(s_i2c, APP_DS3231_ADDRESS << 1U, reg,
                             I2C_MEMADD_SIZE_8BIT, data, length,
                             APP_I2C_TIMEOUT_MS) == HAL_OK);
}

static bool write_regs(uint8_t reg, const uint8_t *data, uint16_t length)
{
    return (s_i2c != NULL) &&
           (HAL_I2C_Mem_Write(s_i2c, APP_DS3231_ADDRESS << 1U, reg,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, length,
                              APP_I2C_TIMEOUT_MS) == HAL_OK);
}

bool ds3231_init(I2C_HandleTypeDef *i2c)
{
    s_i2c = i2c;
    if ((s_i2c == NULL) ||
        (HAL_I2C_IsDeviceReady(s_i2c, APP_DS3231_ADDRESS << 1U, 3U,
                               APP_I2C_TIMEOUT_MS) != HAL_OK)) {
        return false;
    }
    /* INTCN=0, RS=00: SQW outputs 1 Hz for the optional PB1 input. */
    uint8_t control = 0x00U;
    return write_regs(DS3231_REG_CONTROL, &control, 1U);
}

bool ds3231_read(app_datetime_t *datetime)
{
    uint8_t data[7];
    if ((datetime == NULL) || !read_regs(DS3231_REG_TIME, data, sizeof(data))) {
        return false;
    }
    datetime->second = bcd_to_bin(data[0] & 0x7FU);
    datetime->minute = bcd_to_bin(data[1] & 0x7FU);
    if ((data[2] & 0x40U) != 0U) {
        uint8_t hour = bcd_to_bin(data[2] & 0x1FU);
        bool pm = (data[2] & 0x20U) != 0U;
        datetime->hour = (uint8_t)((hour % 12U) + (pm ? 12U : 0U));
    } else {
        datetime->hour = bcd_to_bin(data[2] & 0x3FU);
    }
    datetime->weekday = bcd_to_bin(data[3] & 0x07U);
    datetime->day = bcd_to_bin(data[4] & 0x3FU);
    datetime->month = bcd_to_bin(data[5] & 0x1FU);
    datetime->year = (uint16_t)(2000U + bcd_to_bin(data[6]));
    return (datetime->second < 60U) && (datetime->minute < 60U) &&
           (datetime->hour < 24U) && (datetime->month >= 1U) &&
           (datetime->month <= 12U) && (datetime->day >= 1U) &&
           (datetime->day <= 31U);
}

bool ds3231_set(const app_datetime_t *datetime)
{
    if ((datetime == NULL) || (datetime->year < 2000U) ||
        (datetime->year > 2099U)) {
        return false;
    }
    uint8_t data[7] = {
        bin_to_bcd(datetime->second),
        bin_to_bcd(datetime->minute),
        bin_to_bcd(datetime->hour),
        bin_to_bcd((datetime->weekday >= 1U && datetime->weekday <= 7U) ? datetime->weekday : 1U),
        bin_to_bcd(datetime->day),
        bin_to_bcd(datetime->month),
        bin_to_bcd((uint8_t)(datetime->year - 2000U))
    };
    if (!write_regs(DS3231_REG_TIME, data, sizeof(data))) {
        return false;
    }
    uint8_t status;
    if (read_regs(DS3231_REG_STATUS, &status, 1U)) {
        status &= (uint8_t)~0x80U;
        (void)write_regs(DS3231_REG_STATUS, &status, 1U);
    }
    return true;
}

bool ds3231_lost_power(bool *lost_power)
{
    uint8_t status;
    if ((lost_power == NULL) || !read_regs(DS3231_REG_STATUS, &status, 1U)) {
        return false;
    }
    *lost_power = (status & 0x80U) != 0U;
    return true;
}

bool ds3231_set_build_time(void)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = __DATE__;
    const char *time = __TIME__;
    app_datetime_t value = {0};
    const char *month = strstr(months, date);
    if (month == NULL) {
        return false;
    }
    value.month = (uint8_t)(((size_t)(month - months) / 3U) + 1U);
    value.day = (uint8_t)(((date[4] == ' ') ? 0U : (uint8_t)(date[4] - '0') * 10U) +
                          (uint8_t)(date[5] - '0'));
    value.year = (uint16_t)((date[7] - '0') * 1000 + (date[8] - '0') * 100 +
                            (date[9] - '0') * 10 + (date[10] - '0'));
    value.hour = (uint8_t)((time[0] - '0') * 10 + (time[1] - '0'));
    value.minute = (uint8_t)((time[3] - '0') * 10 + (time[4] - '0'));
    value.second = (uint8_t)((time[6] - '0') * 10 + (time[7] - '0'));
    value.weekday = 1U;
    return ds3231_set(&value);
}

