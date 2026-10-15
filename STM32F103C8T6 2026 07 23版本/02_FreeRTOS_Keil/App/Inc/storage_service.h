#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "app_types.h"
#include "stm32f1xx_hal.h"
#include "ui_service.h"

typedef enum {
    STORAGE_PROCESS_IDLE = 0,
    STORAGE_PROCESS_APPEND_DONE,
    STORAGE_PROCESS_SETTINGS_DONE,
    STORAGE_PROCESS_ERROR
} storage_process_result_t;

bool storage_service_init(SPI_HandleTypeDef *spi, uint32_t *jedec_id);
bool storage_service_load_ui_settings(ui_settings_t *settings);
bool storage_service_queue_append(const app_snapshot_t *snapshot);
bool storage_service_queue_ui_settings(const ui_settings_t *settings);
storage_process_result_t storage_service_process(uint32_t now_ms);
bool storage_service_busy(void);
uint32_t storage_service_record_count(void);

#endif
