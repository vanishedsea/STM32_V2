#include "sensor_service.h"
#include <string.h>
#include "bh1750.h"
#include "bmp280.h"
#include "debug_log.h"
#include "ds3231.h"
#include "sht30.h"

static uint8_t s_available;

uint8_t sensor_service_init(I2C_HandleTypeDef *i2c)
{
    s_available = 0U;
    if (ds3231_init(i2c)) {
        bool lost = false;
        if (ds3231_lost_power(&lost) && lost) {
            debug_log("DS3231 oscillator-stop flag set; applying firmware build time.\r\n");
            (void)ds3231_set_build_time();
        }
        s_available |= APP_VALID_RTC;
    } else {
        debug_log("DS3231 not detected.\r\n");
    }
    if (sht30_init(i2c)) {
        s_available |= APP_VALID_SHT30;
    } else {
        debug_log("SHT30 not detected at 0x44.\r\n");
    }
    if (bmp280_init(i2c)) {
        s_available |= APP_VALID_BMP280;
    } else {
        debug_log("BMP280 not detected at 0x76 or chip ID mismatch.\r\n");
    }
    if (bh1750_init(i2c)) {
        s_available |= APP_VALID_BH1750;
    } else {
        debug_log("BH1750 not detected at 0x23.\r\n");
    }
    return s_available;
}

uint8_t sensor_service_read(app_snapshot_t *snapshot, uint32_t sequence)
{
    if (snapshot == NULL) {
        return 0U;
    }
    app_snapshot_t next;
    memset(&next, 0, sizeof(next));
    next.sequence = sequence;

    if (((s_available & APP_VALID_RTC) != 0U) && ds3231_read(&next.time)) {
        next.valid_mask |= APP_VALID_RTC;
    }
    if (((s_available & APP_VALID_SHT30) != 0U) &&
        sht30_read(&next.temperature_c, &next.humidity_rh)) {
        next.valid_mask |= APP_VALID_SHT30;
    }
    if (((s_available & APP_VALID_BMP280) != 0U) &&
        bmp280_read(&next.bmp_temperature_c, &next.pressure_pa)) {
        next.valid_mask |= APP_VALID_BMP280;
    }
    if (((s_available & APP_VALID_BH1750) != 0U) &&
        bh1750_read(&next.illuminance_lux)) {
        next.valid_mask |= APP_VALID_BH1750;
    }
    *snapshot = next;
    return next.valid_mask;
}

