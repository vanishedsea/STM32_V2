#include "bmp280.h"
#include "app_config.h"

#define BMP280_REG_CALIB       0x88U
#define BMP280_REG_ID          0xD0U
#define BMP280_REG_RESET       0xE0U
#define BMP280_REG_CTRL_MEAS   0xF4U
#define BMP280_REG_CONFIG      0xF5U
#define BMP280_REG_DATA        0xF7U

typedef struct {
    uint16_t t1;
    int16_t t2;
    int16_t t3;
    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
    int32_t t_fine;
} bmp_calibration_t;

static I2C_HandleTypeDef *s_i2c;
static bmp_calibration_t s_cal;

static uint16_t u16_le(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
}

static int16_t s16_le(const uint8_t *data)
{
    return (int16_t)u16_le(data);
}

static bool read_regs(uint8_t reg, uint8_t *data, uint16_t length)
{
    return HAL_I2C_Mem_Read(s_i2c, APP_BMP280_ADDRESS << 1U, reg,
                            I2C_MEMADD_SIZE_8BIT, data, length,
                            APP_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(s_i2c, APP_BMP280_ADDRESS << 1U, reg,
                             I2C_MEMADD_SIZE_8BIT, &value, 1U,
                             APP_I2C_TIMEOUT_MS) == HAL_OK;
}

bool bmp280_init(I2C_HandleTypeDef *i2c)
{
    uint8_t id;
    uint8_t data[24];
    s_i2c = i2c;
    if ((s_i2c == NULL) || !read_regs(BMP280_REG_ID, &id, 1U) || (id != 0x58U)) {
        return false;
    }
    if (!write_reg(BMP280_REG_RESET, 0xB6U)) {
        return false;
    }
    HAL_Delay(5U);
    if (!read_regs(BMP280_REG_CALIB, data, sizeof(data))) {
        return false;
    }
    s_cal.t1 = u16_le(&data[0]);
    s_cal.t2 = s16_le(&data[2]);
    s_cal.t3 = s16_le(&data[4]);
    s_cal.p1 = u16_le(&data[6]);
    s_cal.p2 = s16_le(&data[8]);
    s_cal.p3 = s16_le(&data[10]);
    s_cal.p4 = s16_le(&data[12]);
    s_cal.p5 = s16_le(&data[14]);
    s_cal.p6 = s16_le(&data[16]);
    s_cal.p7 = s16_le(&data[18]);
    s_cal.p8 = s16_le(&data[20]);
    s_cal.p9 = s16_le(&data[22]);
    if (s_cal.p1 == 0U) {
        return false;
    }
    /* 1000 ms standby, filter off; temp x1, pressure x1, normal mode. */
    return write_reg(BMP280_REG_CONFIG, 0xA0U) &&
           write_reg(BMP280_REG_CTRL_MEAS, 0x27U);
}

static int32_t compensate_temperature(int32_t adc_t)
{
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)s_cal.t1 << 1))) *
                    (int32_t)s_cal.t2) >> 11;
    int32_t var2 = (((((adc_t >> 4) - (int32_t)s_cal.t1) *
                      ((adc_t >> 4) - (int32_t)s_cal.t1)) >> 12) *
                    (int32_t)s_cal.t3) >> 14;
    s_cal.t_fine = var1 + var2;
    return (s_cal.t_fine * 5 + 128) >> 8;
}

static uint32_t compensate_pressure(int32_t adc_p)
{
    int64_t var1 = (int64_t)s_cal.t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_cal.p6;
    var2 += (var1 * (int64_t)s_cal.p5) << 17;
    var2 += ((int64_t)s_cal.p4) << 35;
    var1 = ((var1 * var1 * (int64_t)s_cal.p3) >> 8) +
           ((var1 * (int64_t)s_cal.p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)s_cal.p1) >> 33;
    if (var1 == 0) {
        return 0U;
    }
    int64_t pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)s_cal.p9 * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = ((int64_t)s_cal.p8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + ((int64_t)s_cal.p7 << 4);
    return (uint32_t)(pressure >> 8);
}

bool bmp280_read(float *temperature_c, float *pressure_pa)
{
    uint8_t data[6];
    if ((temperature_c == NULL) || (pressure_pa == NULL) || (s_i2c == NULL) ||
        !read_regs(BMP280_REG_DATA, data, sizeof(data))) {
        return false;
    }
    int32_t adc_p = (int32_t)(((uint32_t)data[0] << 12U) |
                              ((uint32_t)data[1] << 4U) | (data[2] >> 4U));
    int32_t adc_t = (int32_t)(((uint32_t)data[3] << 12U) |
                              ((uint32_t)data[4] << 4U) | (data[5] >> 4U));
    if ((adc_t == 0x80000) || (adc_p == 0x80000)) {
        return false;
    }
    int32_t temp_x100 = compensate_temperature(adc_t);
    uint32_t pressure = compensate_pressure(adc_p);
    if (pressure == 0U) {
        return false;
    }
    *temperature_c = (float)temp_x100 / 100.0f;
    *pressure_pa = (float)pressure;
    return true;
}

