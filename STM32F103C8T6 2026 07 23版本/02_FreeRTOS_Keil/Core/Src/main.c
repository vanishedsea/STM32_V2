#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "app_config.h"
#include "app_types.h"
#include "board.h"
#include "buzzer.h"
#include "debug_log.h"
#include "encoder.h"
#include "sensor_service.h"
#include "ssd1306.h"
#include "storage_service.h"
#include "ui_service.h"

#define SENSOR_STACK_WORDS  384U
#define UI_STACK_WORDS      384U
#define STORAGE_STACK_WORDS 320U
#define HEALTH_STACK_WORDS  128U
#define ENCODER_STACK_WORDS 160U
#define ENCODER_QUEUE_LENGTH 8U

typedef struct {
    ui_input_t input;
    uint16_t repeat_count;
} ui_command_t;

static StaticTask_t s_sensor_tcb;
static StaticTask_t s_ui_tcb;
static StaticTask_t s_storage_tcb;
static StaticTask_t s_health_tcb;
static StaticTask_t s_encoder_tcb;
static StackType_t s_sensor_stack[SENSOR_STACK_WORDS];
static StackType_t s_ui_stack[UI_STACK_WORDS];
static StackType_t s_storage_stack[STORAGE_STACK_WORDS];
static StackType_t s_health_stack[HEALTH_STACK_WORDS];
static StackType_t s_encoder_stack[ENCODER_STACK_WORDS];

static StaticQueue_t s_ui_queue_control;
static StaticQueue_t s_log_queue_control;
static StaticQueue_t s_encoder_queue_control;
static StaticQueue_t s_settings_queue_control;
static uint8_t s_ui_queue_storage[sizeof(app_snapshot_t)];
static uint8_t s_log_queue_storage[2U * sizeof(app_snapshot_t)];
static uint8_t s_encoder_queue_storage[ENCODER_QUEUE_LENGTH * sizeof(ui_command_t)];
static uint8_t s_settings_queue_storage[sizeof(ui_settings_t)];
static QueueHandle_t s_ui_queue;
static QueueHandle_t s_log_queue;
static QueueHandle_t s_encoder_queue;
static QueueHandle_t s_settings_queue;

static StaticSemaphore_t s_i2c_mutex_storage;
static SemaphoreHandle_t s_i2c_mutex;

static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];

static volatile bool s_flash_ready;
static bool s_ui_ready;

static void report_storage_failure(const char *operation)
{
    s_flash_ready = false;
    debug_log("W25Q64 %s stopped after erase/program/verify error.\r\n", operation);
    if (ui_service_buzzer_enabled()) {
        buzzer_pulse_start(APP_ERROR_BEEP_MS, HAL_GetTick());
    }
}

static bool encoder_event_to_ui_input(encoder_event_t event, ui_input_t *input)
{
    if (input == NULL) {
        return false;
    }
    switch (event) {
    case ENCODER_EVENT_CLICK:
        *input = UI_INPUT_CLICK;
        return true;
    case ENCODER_EVENT_LONG_PRESS:
        *input = UI_INPUT_LONG_PRESS;
        return true;
    default:
        return false;
    }
}

static void task_encoder(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        uint32_t now_ms = HAL_GetTick();
        encoder_update(now_ms);
        buzzer_update(now_ms);

        int16_t rotation = encoder_take_rotation();
        if (rotation != 0) {
            int32_t signed_rotation = rotation;
            ui_command_t command;
            command.input = signed_rotation > 0
                                ? UI_INPUT_CLOCKWISE
                                : UI_INPUT_COUNTERCLOCKWISE;
            command.repeat_count = (uint16_t)(signed_rotation > 0
                                                 ? signed_rotation
                                                 : -signed_rotation);
            (void)xQueueSend(s_encoder_queue, &command, 0U);
        }

        encoder_event_t event;
        while (encoder_get_event(&event)) {
            ui_command_t command;
            if (encoder_event_to_ui_input(event, &command.input)) {
                command.repeat_count = 1U;
                (void)xQueueSend(s_encoder_queue, &command, 0U);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_ENCODER_SCAN_PERIOD_MS));
    }
}

static void task_sensor(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t sequence = 0U;
    const uint32_t samples_per_log = APP_LOG_PERIOD_MS / APP_SENSOR_PERIOD_MS;
    for (;;) {
        app_snapshot_t snapshot;
        ++sequence;
        if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(500U)) == pdTRUE) {
            (void)sensor_service_read(&snapshot, sequence);
            (void)xSemaphoreGive(s_i2c_mutex);
            (void)xQueueOverwrite(s_ui_queue, &snapshot);
            if ((samples_per_log != 0U) && ((sequence % samples_per_log) == 0U)) {
                (void)xQueueSend(s_log_queue, &snapshot, 0U);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SENSOR_PERIOD_MS));
    }
}

static void task_ui(void *argument)
{
    (void)argument;
    app_snapshot_t snapshot = {0};
    for (;;) {
        bool content_redraw =
            xQueueReceive(s_ui_queue, &snapshot,
                          pdMS_TO_TICKS(APP_UI_PERIOD_MS)) == pdTRUE;
        ui_command_t command;
        while (xQueueReceive(s_encoder_queue, &command, 0U) == pdTRUE) {
            uint16_t repeat;
            for (repeat = 0U; repeat < command.repeat_count; ++repeat) {
                content_redraw =
                    ui_service_handle_input(command.input) || content_redraw;
            }
        }
        ui_settings_t settings;
        if (ui_service_take_settings_changed(&settings)) {
            (void)xQueueOverwrite(s_settings_queue, &settings);
            content_redraw = true;
        }
        /*
         * The shared UI service advances exactly one Nokia transition frame
         * per call. A timeout therefore becomes the animation frame clock,
         * while sensor and encoder events may still wake the render path.
         */
        content_redraw = ui_service_animation_active() || content_redraw;
        bool fps_redraw = ui_service_fps_tick(HAL_GetTick());
        if ((content_redraw || fps_redraw) && s_ui_ready &&
            (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(500U)) == pdTRUE)) {
            bool ok = ui_service_show(&snapshot, s_flash_ready,
                                      storage_service_record_count());
            (void)xSemaphoreGive(s_i2c_mutex);
            if (!ok) {
                debug_log("OLED update failed.\r\n");
            } else if (content_redraw) {
                ui_service_record_frame();
            }
        }
    }
}

static void task_storage(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    app_snapshot_t snapshot;
    for (;;) {
        if (s_flash_ready &&
            (xQueueReceive(s_log_queue, &snapshot, 0U) == pdTRUE) &&
            !storage_service_queue_append(&snapshot)) {
            report_storage_failure("log queue");
        }

        ui_settings_t settings;
        if (s_flash_ready &&
            (xQueueReceive(s_settings_queue, &settings, 0U) == pdTRUE) &&
            !storage_service_queue_ui_settings(&settings)) {
            report_storage_failure("settings queue");
        }

        if (s_flash_ready) {
            storage_process_result_t result =
                storage_service_process(HAL_GetTick());
            if (result == STORAGE_PROCESS_APPEND_DONE) {
                debug_log("log appended; count=%lu\r\n",
                          (unsigned long)storage_service_record_count());
            } else if (result == STORAGE_PROCESS_SETTINGS_DONE) {
                debug_log("UI settings saved in background.\r\n");
            } else if (result == STORAGE_PROCESS_ERROR) {
                report_storage_failure("background service");
            }
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(APP_STORAGE_SERVICE_PERIOD_MS));
    }
}

static void task_health(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        board_led_toggle();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_HEARTBEAT_PERIOD_MS));
    }
}

static void print_i2c_scan(void)
{
    uint8_t addresses[16];
    uint8_t found = board_i2c_scan(addresses, sizeof(addresses));
    debug_log("I2C scan: %u device(s):", found);
    uint8_t shown = (found < sizeof(addresses)) ? found : (uint8_t)sizeof(addresses);
    for (uint8_t i = 0U; i < shown; ++i) {
        debug_log(" 0x%02X", addresses[i]);
    }
    debug_log("\r\nExpected: 0x23 0x3C/0x3D 0x44 0x68 0x76\r\n");
}

int main(void)
{
    board_init();
    board_led_set(false);
    buzzer_set(false);
    debug_log("\r\nSTM32F103 multisensor FREERTOS build %s %s\r\n", __DATE__, __TIME__);
    debug_log("WS2812B disabled; PA8 is analog input.\r\n");
    debug_log("EC11: PB12=A PB13=B; separate button PB15->GND.\r\n");
    print_i2c_scan();

    s_ui_ready = ui_service_init(&hi2c1, "FREERTOS");
    debug_log("OLED status=%s address=0x%02X\r\n",
              s_ui_ready ? "OK" : "ERROR", s_ui_ready ? ssd1306_address() : 0U);
    uint8_t sensors = sensor_service_init(&hi2c1);
    debug_log("Sensor availability mask: 0x%02X\r\n", sensors);

    uint32_t jedec_id = 0U;
    s_flash_ready = storage_service_init(&hspi1, &jedec_id);
    debug_log("SPI flash JEDEC=0x%06lX status=%s records=%lu\r\n",
              (unsigned long)jedec_id, s_flash_ready ? "OK" : "ERROR",
              (unsigned long)storage_service_record_count());

    ui_settings_t settings;
    if (s_flash_ready && storage_service_load_ui_settings(&settings)) {
        ui_service_apply_settings(&settings);
        debug_log("UI settings loaded: style=%s beep=%s.\r\n",
                  settings.theme == UI_THEME_NOKIA ? "NOKIA" : "CLASSIC",
                  settings.buzzer_enabled ? "ON" : "OFF");
    }
    if (ui_service_buzzer_enabled()) {
        buzzer_pulse_start(APP_BOOT_BEEP_MS, HAL_GetTick());
    }

    s_ui_queue = xQueueCreateStatic(1U, sizeof(app_snapshot_t),
                                    s_ui_queue_storage, &s_ui_queue_control);
    s_log_queue = xQueueCreateStatic(2U, sizeof(app_snapshot_t),
                                     s_log_queue_storage, &s_log_queue_control);
    s_encoder_queue = xQueueCreateStatic(ENCODER_QUEUE_LENGTH, sizeof(ui_command_t),
                                         s_encoder_queue_storage,
                                         &s_encoder_queue_control);
    s_settings_queue = xQueueCreateStatic(1U, sizeof(ui_settings_t),
                                          s_settings_queue_storage,
                                          &s_settings_queue_control);
    s_i2c_mutex = xSemaphoreCreateMutexStatic(&s_i2c_mutex_storage);
    if ((s_ui_queue == NULL) || (s_log_queue == NULL) ||
        (s_encoder_queue == NULL) || (s_settings_queue == NULL) ||
        (s_i2c_mutex == NULL)) {
        Error_Handler();
    }
    vQueueAddToRegistry(s_ui_queue, "ui");
    vQueueAddToRegistry(s_log_queue, "log");
    vQueueAddToRegistry(s_encoder_queue, "encoder");
    vQueueAddToRegistry(s_settings_queue, "settings");
    encoder_init();
    debug_log("Separate PB15 button ready: short=select long=back.\r\n");

    if ((xTaskCreateStatic(task_encoder, "encoder", ENCODER_STACK_WORDS, NULL, 4U,
                           s_encoder_stack, &s_encoder_tcb) == NULL) ||
        (xTaskCreateStatic(task_sensor, "sensor", SENSOR_STACK_WORDS, NULL, 3U,
                           s_sensor_stack, &s_sensor_tcb) == NULL) ||
        (xTaskCreateStatic(task_ui, "ui", UI_STACK_WORDS, NULL, 2U,
                           s_ui_stack, &s_ui_tcb) == NULL) ||
        (xTaskCreateStatic(task_storage, "storage", STORAGE_STACK_WORDS, NULL, 2U,
                           s_storage_stack, &s_storage_tcb) == NULL) ||
        (xTaskCreateStatic(task_health, "health", HEALTH_STACK_WORDS, NULL, 1U,
                           s_health_stack, &s_health_tcb) == NULL)) {
        Error_Handler();
    }

    vTaskStartScheduler();
    Error_Handler();
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer,
                                   StackType_t **stack_buffer,
                                   uint32_t *stack_size)
{
    *task_buffer = &s_idle_tcb;
    *stack_buffer = s_idle_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    buzzer_set(false);
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        for (volatile uint32_t delay = 0U; delay < 600000U; ++delay) {
            __NOP();
        }
    }
}
