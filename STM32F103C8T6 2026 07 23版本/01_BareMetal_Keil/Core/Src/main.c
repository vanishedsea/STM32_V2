#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "app_types.h"
#include "board.h"
#include "buzzer.h"
#include "debug_log.h"
#include "encoder.h"
#include "event_bus.h"
#include "scheduler.h"
#include "sensor_service.h"
#include "ssd1306.h"
#include "storage_service.h"
#include "ui_service.h"

static app_snapshot_t s_snapshot;
static uint32_t s_sequence;
static bool s_ui_ready;
static bool s_ui_dirty;
static bool s_flash_ready;

static app_event_type_t encoder_event_to_app_event(encoder_event_t event)
{
    switch (event) {
    case ENCODER_EVENT_CLICK:
        return APP_EVENT_ENCODER_CLICK;
    case ENCODER_EVENT_LONG_PRESS:
        return APP_EVENT_ENCODER_LONG_PRESS;
    default:
        return APP_EVENT_NONE;
    }
}

static void task_encoder(void *context)
{
    (void)context;
    encoder_event_t encoder_event;
    encoder_update(HAL_GetTick());
    int16_t rotation = encoder_take_rotation();
    if (rotation != 0) {
        (void)event_bus_post((app_event_t){
            APP_EVENT_ENCODER_ROTATE,
            (uint32_t)(int32_t)rotation
        });
    }
    while (encoder_get_event(&encoder_event)) {
        app_event_type_t app_event = encoder_event_to_app_event(encoder_event);
        if (app_event != APP_EVENT_NONE) {
            (void)event_bus_post((app_event_t){app_event, 0U});
        }
    }
}

static void task_sensor(void *context)
{
    (void)context;
    ++s_sequence;
    uint8_t valid = sensor_service_read(&s_snapshot, s_sequence);
    (void)event_bus_post((app_event_t){APP_EVENT_NEW_SNAPSHOT, s_sequence});
    if ((s_sequence % 10U) == 0U) {
        ssd1306_stats_t oled_stats;
        ssd1306_get_stats(&oled_stats);
        debug_log("sample=%lu valid=0x%02X event_drop=%lu enc_drop=%lu "
                  "oled_update=%lu runs=%lu bytes=%lu\r\n",
                  (unsigned long)s_sequence, valid,
                  (unsigned long)event_bus_dropped(),
                  (unsigned long)encoder_dropped_events(),
                  (unsigned long)oled_stats.update_calls,
                  (unsigned long)oled_stats.transmitted_runs,
                  (unsigned long)oled_stats.pixel_bytes);
    }
}

static void task_ui(void *context)
{
    (void)context;
    if (!s_ui_ready) {
        return;
    }
    bool content_dirty = s_ui_dirty;
    bool fps_dirty = ui_service_fps_tick(HAL_GetTick());
    if (content_dirty || fps_dirty) {
        if (!ui_service_show(&s_snapshot, s_flash_ready,
                             storage_service_record_count())) {
            debug_log("OLED update failed.\r\n");
            s_ui_ready = false;
            s_ui_dirty = false;
            return;
        }
        if (content_dirty) {
            ui_service_record_frame();
        }
        s_ui_dirty = ui_service_animation_active();
    }
}

static void report_storage_error(void)
{
    s_flash_ready = false;
    (void)event_bus_post((app_event_t){APP_EVENT_STORAGE_ERROR, 0U});
}

static void task_log_request(void *context)
{
    (void)context;
    if (s_flash_ready && (s_snapshot.sequence != 0U)) {
        if (!storage_service_queue_append(&s_snapshot)) {
            report_storage_error();
        }
    }
}

static void task_settings(void *context)
{
    (void)context;
    ui_settings_t settings;
    if (s_flash_ready && ui_service_take_settings_changed(&settings)) {
        if (!storage_service_queue_ui_settings(&settings)) {
            report_storage_error();
        }
    }
}

static void task_storage_service(void *context)
{
    (void)context;
    if (!s_flash_ready) {
        return;
    }
    storage_process_result_t result = storage_service_process(HAL_GetTick());
    if (result == STORAGE_PROCESS_APPEND_DONE) {
        debug_log("log appended; count=%lu\r\n",
                  (unsigned long)storage_service_record_count());
        s_ui_dirty = true;
    } else if (result == STORAGE_PROCESS_SETTINGS_DONE) {
        debug_log("UI settings saved in background.\r\n");
    } else if (result == STORAGE_PROCESS_ERROR) {
        report_storage_error();
    }
}

static void task_heartbeat(void *context)
{
    (void)context;
    board_led_toggle();
}

static void task_buzzer(void *context)
{
    (void)context;
    buzzer_update(HAL_GetTick());
}

static void apply_rotation(int32_t rotation)
{
    ui_input_t input = (rotation > 0) ? UI_INPUT_CLOCKWISE :
                                         UI_INPUT_COUNTERCLOCKWISE;
    uint32_t steps = (rotation > 0) ? (uint32_t)rotation :
                                     (uint32_t)(-rotation);
    while (steps-- > 0U) {
        (void)ui_service_handle_input(input);
    }
}

static void dispatch_events(void)
{
    app_event_t event;
    while (event_bus_pop(&event)) {
        switch (event.type) {
        case APP_EVENT_NEW_SNAPSHOT:
            s_ui_dirty = true;
            break;
        case APP_EVENT_STORAGE_ERROR:
            debug_log("W25Q64 logging stopped after an erase/program/verify error.\r\n");
            if (ui_service_buzzer_enabled()) {
                buzzer_pulse_start(APP_ERROR_BEEP_MS, HAL_GetTick());
            }
            s_ui_dirty = true;
            break;
        case APP_EVENT_ENCODER_ROTATE:
            apply_rotation((int32_t)event.value);
            s_ui_dirty = true;
            break;
        case APP_EVENT_ENCODER_CLOCKWISE:
            (void)ui_service_handle_input(UI_INPUT_CLOCKWISE);
            s_ui_dirty = true;
            break;
        case APP_EVENT_ENCODER_COUNTERCLOCKWISE:
            (void)ui_service_handle_input(UI_INPUT_COUNTERCLOCKWISE);
            s_ui_dirty = true;
            break;
        case APP_EVENT_ENCODER_CLICK:
            (void)ui_service_handle_input(UI_INPUT_CLICK);
            s_ui_dirty = true;
            break;
        case APP_EVENT_ENCODER_LONG_PRESS:
            (void)ui_service_handle_input(UI_INPUT_LONG_PRESS);
            s_ui_dirty = true;
            break;
        default:
            break;
        }
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
    debug_log("\r\nSTM32F103 multisensor BARE-METAL build %s %s\r\n", __DATE__, __TIME__);
    debug_log("WS2812B disabled; PA8 is analog input.\r\n");
    debug_log("EC11: PB12=A PB13=B; separate button PB15->GND.\r\n");
    print_i2c_scan();

    s_ui_ready = ui_service_init(&hi2c1, "BARE METAL");
    if (s_ui_ready) {
        debug_log("OLED ready at 0x%02X.\r\n", ssd1306_address());
    } else {
        debug_log("OLED initialization failed.\r\n");
    }

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
        s_ui_dirty = true;
        debug_log("UI settings loaded: style=%s beep=%s.\r\n",
                  settings.theme == UI_THEME_NOKIA ? "NOKIA" : "CLASSIC",
                  settings.buzzer_enabled ? "ON" : "OFF");
    }

    if (ui_service_buzzer_enabled()) {
        buzzer_pulse_start(APP_BOOT_BEEP_MS, HAL_GetTick());
    }
    event_bus_init();
    encoder_init();
    debug_log("Separate PB15 button ready: short=select long=back.\r\n");
    scheduler_init();
    uint32_t now = HAL_GetTick();
    if (!scheduler_add(task_encoder, NULL, APP_ENCODER_SCAN_PERIOD_MS, now) ||
        !scheduler_add(task_sensor, NULL, APP_SENSOR_PERIOD_MS, now) ||
        !scheduler_add(task_ui, NULL, APP_UI_PERIOD_MS, now + APP_UI_PERIOD_MS) ||
        !scheduler_add(task_settings, NULL, APP_UI_SETTINGS_PERIOD_MS,
                       now + APP_UI_SETTINGS_PERIOD_MS) ||
        !scheduler_add(task_storage_service, NULL, APP_STORAGE_SERVICE_PERIOD_MS,
                       now + APP_STORAGE_SERVICE_PERIOD_MS) ||
        !scheduler_add(task_log_request, NULL, APP_LOG_PERIOD_MS,
                       now + APP_LOG_PERIOD_MS) ||
        !scheduler_add(task_buzzer, NULL, APP_BUZZER_SERVICE_PERIOD_MS, now) ||
        !scheduler_add(task_heartbeat, NULL, APP_HEARTBEAT_PERIOD_MS, now + APP_HEARTBEAT_PERIOD_MS)) {
        Error_Handler();
    }

    for (;;) {
        scheduler_run_ready(HAL_GetTick());
        dispatch_events();
        __WFI();
    }
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
