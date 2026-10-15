#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* The connected 3-pin active buzzer module is triggered by a LOW level. */
#define APP_BUZZER_ACTIVE_HIGH          0
#define APP_BOOT_BEEP_MS                40U
#define APP_ERROR_BEEP_MS               100U
#define APP_BUZZER_MAX_ON_MS            200U
#define APP_BUZZER_SERVICE_PERIOD_MS    5U
#define APP_SENSOR_PERIOD_MS            1000U
#define APP_UI_PERIOD_MS                20U
#define APP_LOG_PERIOD_MS               60000U
#define APP_STORAGE_SERVICE_PERIOD_MS   5U
#define APP_HEARTBEAT_PERIOD_MS         500U
#define APP_ENCODER_SCAN_PERIOD_MS      2U
#define APP_ENCODER_BUTTON_DEBOUNCE_MS  20U
#define APP_ENCODER_LONG_PRESS_MS       800U
#define APP_ENCODER_REVERSE             0

#ifndef APP_UI_DEFAULT_THEME
#define APP_UI_DEFAULT_THEME            0U
#endif
#ifndef APP_UI_PROFILE_ID
#define APP_UI_PROFILE_ID               0U
#endif
#define APP_UI_ANIMATION_FRAMES         6U
#define APP_UI_SETTINGS_PERIOD_MS       500U
#define APP_UI_FPS_SAMPLE_MS            1000U

#define APP_I2C_CLOCK_HZ                400000U
#define APP_I2C_TIMEOUT_MS              50U
#define APP_SPI_TIMEOUT_MS              100U
#define APP_OLED_DIRTY_MERGE_GAP        6U

#define APP_DS3231_ADDRESS              0x68U
#define APP_SHT30_ADDRESS               0x44U
#define APP_BMP280_ADDRESS              0x76U
#define APP_BH1750_ADDRESS              0x23U
#define APP_OLED_ADDRESS_PRIMARY        0x3CU
#define APP_OLED_ADDRESS_SECONDARY      0x3DU

#define APP_LOG_REGION_START            0x00010000UL
#define APP_LOG_REGION_SIZE             0x00010000UL
#define APP_SETTINGS_ADDRESS            0x00000000UL

#endif
