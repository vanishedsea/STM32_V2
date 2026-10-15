#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

enum {
    APP_VALID_RTC = 1U << 0,
    APP_VALID_SHT30 = 1U << 1,
    APP_VALID_BMP280 = 1U << 2,
    APP_VALID_BH1750 = 1U << 3,
    APP_VALID_FLASH = 1U << 4,
    APP_VALID_OLED = 1U << 5
};

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} app_datetime_t;

typedef struct {
    uint32_t sequence;
    uint8_t valid_mask;
    app_datetime_t time;
    float temperature_c;
    float humidity_rh;
    float bmp_temperature_c;
    float pressure_pa;
    float illuminance_lux;
} app_snapshot_t;

#endif

