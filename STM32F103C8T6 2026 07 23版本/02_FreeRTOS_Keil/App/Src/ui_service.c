#include "ui_service.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "ssd1306.h"
#include "ui_theme_nokia.h"

typedef enum {
    UI_PAGE_DASHBOARD = 0,
    UI_PAGE_CLIMATE,
    UI_PAGE_AIR_LIGHT,
    UI_PAGE_SYSTEM,
    UI_PAGE_COUNT
} ui_page_t;

#define UI_MENU_ITEM_COUNT 8U
#define UI_CLASSIC_VISIBLE_ITEMS 6U

static bool s_ready;
static const char *s_edition_tag = "BM";
static ui_page_t s_page = UI_PAGE_DASHBOARD;
static bool s_menu_open;
static uint8_t s_menu_index;
static volatile bool s_buzzer_enabled = true;
static ui_theme_t s_theme = UI_THEME_CLASSIC;
static bool s_settings_changed;
static bool s_animation_pending;
static uint8_t s_animation_from;
static uint8_t s_animation_to;
static uint8_t s_animation_frame;
static bool s_fps_counter_started;
static bool s_fps_valid;
static uint16_t s_fps_frame_count;
static uint32_t s_fps_window_start_ms;
static char s_fps_label[4] = "F--";

static uint8_t page_to_menu_index(ui_page_t page)
{
    static const uint8_t indexes[UI_PAGE_COUNT] = {0U, 1U, 3U, 2U};
    return indexes[(uint8_t)page];
}

static ui_page_t menu_index_to_page(uint8_t index)
{
    static const ui_page_t pages[4] = {
        UI_PAGE_DASHBOARD, UI_PAGE_CLIMATE, UI_PAGE_SYSTEM, UI_PAGE_AIR_LIGHT
    };
    return pages[index];
}

static int32_t round_to_i32(float value)
{
    return (int32_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static void format_fixed_1(char *buffer, size_t size, float value)
{
    int32_t scaled = round_to_i32(value * 10.0f);
    bool negative = scaled < 0;
    uint32_t magnitude = (uint32_t)(negative ? -scaled : scaled);
    (void)snprintf(buffer, size, "%s%lu.%lu", negative ? "-" : "",
                   (unsigned long)(magnitude / 10U),
                   (unsigned long)(magnitude % 10U));
}

static uint8_t centered_x(const char *text, uint8_t scale)
{
    size_t length = (text != NULL) ? strlen(text) : 0U;
    uint16_t width = (length == 0U) ? 0U :
                     (uint16_t)(length * 6U * scale - scale);
    return (width < SSD1306_WIDTH) ?
           (uint8_t)((SSD1306_WIDTH - width) / 2U) : 0U;
}

static bool is_valid(const app_snapshot_t *snapshot, uint8_t mask)
{
    return (snapshot->valid_mask & mask) != 0U;
}

static uint8_t available_sensor_count(uint8_t valid_mask)
{
    static const uint8_t masks[] = {
        APP_VALID_RTC, APP_VALID_SHT30, APP_VALID_BMP280, APP_VALID_BH1750
    };
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(masks) / sizeof(masks[0])); ++i) {
        if ((valid_mask & masks[i]) != 0U) {
            ++count;
        }
    }
    return count;
}

static void draw_page_header(const char *title, const app_snapshot_t *snapshot)
{
    char line[16];
    ssd1306_draw_text(2U, 1U, title);
    if (is_valid(snapshot, APP_VALID_RTC)) {
        (void)snprintf(line, sizeof(line), "%02u:%02u:%02u",
                       snapshot->time.hour, snapshot->time.minute,
                       snapshot->time.second);
    } else {
        (void)snprintf(line, sizeof(line), "--:--:--");
    }
    ssd1306_draw_text(60U, 1U, s_fps_label);
    ssd1306_draw_text(80U, 1U, line);
    ssd1306_draw_hline(0U, 9U, SSD1306_WIDTH);
}

static void draw_page_footer(uint8_t page_number)
{
    char line[24];
    ssd1306_draw_hline(0U, 55U, SSD1306_WIDTH);
    (void)snprintf(line, sizeof(line), "P%u/4 PRESS MENU", page_number);
    ssd1306_draw_text(2U, 57U, line);
}

static void draw_value_card(uint8_t x, const char *label, const char *value,
                            const char *unit, bool valid)
{
    ssd1306_draw_rect(x, 11U, 63U, 26U);
    ssd1306_draw_text((uint8_t)(x + 4U), 13U, label);
    if (valid) {
        if (strlen(value) <= 4U) {
            ssd1306_draw_text_scaled((uint8_t)(x + 4U), 21U, value, 2U);
        } else {
            ssd1306_draw_text((uint8_t)(x + 4U), 26U, value);
        }
    } else {
        ssd1306_draw_text_scaled((uint8_t)(x + 5U), 21U, "--", 2U);
    }
    ssd1306_draw_text((uint8_t)(x + 53U), 28U, unit);
}

static void draw_wide_card(uint8_t y, const char *label, const char *value,
                           const char *unit, bool valid)
{
    ssd1306_draw_rect(0U, y, SSD1306_WIDTH, 25U);
    ssd1306_draw_text(4U, (uint8_t)(y + 2U), label);
    ssd1306_draw_text(104U, (uint8_t)(y + 2U), unit);
    if (valid) {
        if (strlen(value) <= 8U) {
            ssd1306_draw_text_scaled(4U, (uint8_t)(y + 10U), value, 2U);
        } else {
            ssd1306_draw_text(4U, (uint8_t)(y + 13U), value);
        }
    } else {
        ssd1306_draw_text_scaled(4U, (uint8_t)(y + 10U), "--", 2U);
    }
}

static void render_dashboard(const app_snapshot_t *snapshot, bool flash_ok,
                             uint32_t log_count)
{
    char line[24];
    char temperature[16];
    char humidity[16];
    char value[16];
    bool rtc_ok = is_valid(snapshot, APP_VALID_RTC);
    bool sht_ok = is_valid(snapshot, APP_VALID_SHT30);
    bool bmp_ok = is_valid(snapshot, APP_VALID_BMP280);
    bool light_ok = is_valid(snapshot, APP_VALID_BH1750);
    bool temperature_ok = sht_ok || bmp_ok;

    if (temperature_ok) {
        format_fixed_1(temperature, sizeof(temperature),
                       sht_ok ? snapshot->temperature_c :
                                snapshot->bmp_temperature_c);
    } else {
        (void)snprintf(temperature, sizeof(temperature), "--");
    }
    if (sht_ok) {
        format_fixed_1(humidity, sizeof(humidity), snapshot->humidity_rh);
    } else {
        (void)snprintf(humidity, sizeof(humidity), "--");
    }

    if (rtc_ok) {
        (void)snprintf(line, sizeof(line), "%02u:%02u:%02u",
                       snapshot->time.hour, snapshot->time.minute,
                       snapshot->time.second);
        ssd1306_draw_text(2U, 1U, line);
        (void)snprintf(line, sizeof(line), "%02u/%02u",
                       snapshot->time.month, snapshot->time.day);
        ssd1306_draw_text(97U, 1U, line);
    } else {
        ssd1306_draw_text(2U, 1U, "RTC --:--:--");
    }
    ssd1306_draw_text(73U, 1U, s_fps_label);
    ssd1306_draw_hline(0U, 9U, SSD1306_WIDTH);
    draw_value_card(0U, "TEMP", temperature, "C", temperature_ok);
    draw_value_card(65U, "HUM", humidity, "%", sht_ok);

    if (bmp_ok) {
        format_fixed_1(value, sizeof(value), snapshot->pressure_pa / 100.0f);
        (void)snprintf(line, sizeof(line), "PRESS %sHPA", value);
    } else {
        (void)snprintf(line, sizeof(line), "PRESS -- HPA");
    }
    ssd1306_draw_text(2U, 39U, line);
    if (light_ok) {
        format_fixed_1(value, sizeof(value), snapshot->illuminance_lux);
        (void)snprintf(line, sizeof(line), "LIGHT %sLX", value);
    } else {
        (void)snprintf(line, sizeof(line), "LIGHT -- LX");
    }
    ssd1306_draw_text(2U, 47U, line);

    ssd1306_draw_hline(0U, 55U, SSD1306_WIDTH);
    (void)snprintf(line, sizeof(line), "%s S:%u/4 F:%s L:%lu",
                   s_edition_tag, available_sensor_count(snapshot->valid_mask),
                   flash_ok ? "OK" : "ER",
                   (unsigned long)(log_count % 10000U));
    ssd1306_draw_text(2U, 57U, line);
}

static void render_climate(const app_snapshot_t *snapshot)
{
    char line[24];
    char temperature[16];
    char humidity[16];
    char bmp_temperature[16];
    bool sht_ok = is_valid(snapshot, APP_VALID_SHT30);
    bool bmp_ok = is_valid(snapshot, APP_VALID_BMP280);

    draw_page_header("CLIMATE", snapshot);
    if (sht_ok) {
        format_fixed_1(temperature, sizeof(temperature), snapshot->temperature_c);
        format_fixed_1(humidity, sizeof(humidity), snapshot->humidity_rh);
    } else {
        (void)snprintf(temperature, sizeof(temperature), "--");
        (void)snprintf(humidity, sizeof(humidity), "--");
    }
    draw_value_card(0U, "TEMP", temperature, "C", sht_ok);
    draw_value_card(65U, "HUM", humidity, "%", sht_ok);

    if (bmp_ok) {
        format_fixed_1(bmp_temperature, sizeof(bmp_temperature),
                       snapshot->bmp_temperature_c);
        (void)snprintf(line, sizeof(line), "BMP TEMP %sC", bmp_temperature);
    } else {
        (void)snprintf(line, sizeof(line), "BMP TEMP -- C");
    }
    ssd1306_draw_text(2U, 40U, line);
    (void)snprintf(line, sizeof(line), "SHT:%s BMP:%s",
                   sht_ok ? "OK" : "ER", bmp_ok ? "OK" : "ER");
    ssd1306_draw_text(2U, 48U, line);
    draw_page_footer(2U);
}

static void render_air_light(const app_snapshot_t *snapshot)
{
    char pressure[16];
    char light[16];
    bool bmp_ok = is_valid(snapshot, APP_VALID_BMP280);
    bool light_ok = is_valid(snapshot, APP_VALID_BH1750);

    draw_page_header("AIR/LIGHT", snapshot);
    if (bmp_ok) {
        format_fixed_1(pressure, sizeof(pressure), snapshot->pressure_pa / 100.0f);
    } else {
        (void)snprintf(pressure, sizeof(pressure), "--");
    }
    if (light_ok) {
        format_fixed_1(light, sizeof(light), snapshot->illuminance_lux);
    } else {
        (void)snprintf(light, sizeof(light), "--");
    }
    draw_wide_card(11U, "PRESSURE", pressure, "HPA", bmp_ok);
    draw_wide_card(38U, "LIGHT", light, "LX", light_ok);
}

static void render_system(const app_snapshot_t *snapshot, bool flash_ok,
                          uint32_t log_count)
{
    char line[24];
    draw_page_header("SYSTEM", snapshot);
    (void)snprintf(line, sizeof(line), "RTC:%s SHT:%s",
                   is_valid(snapshot, APP_VALID_RTC) ? "OK" : "ER",
                   is_valid(snapshot, APP_VALID_SHT30) ? "OK" : "ER");
    ssd1306_draw_text(2U, 13U, line);
    (void)snprintf(line, sizeof(line), "BMP:%s LUX:%s",
                   is_valid(snapshot, APP_VALID_BMP280) ? "OK" : "ER",
                   is_valid(snapshot, APP_VALID_BH1750) ? "OK" : "ER");
    ssd1306_draw_text(2U, 22U, line);
    (void)snprintf(line, sizeof(line), "FLASH:%s LOG:%lu",
                   flash_ok ? "OK" : "ER",
                   (unsigned long)(log_count % 10000U));
    ssd1306_draw_text(2U, 31U, line);
    (void)snprintf(line, sizeof(line), "ENC:OK %s", s_fps_label);
    ssd1306_draw_text(2U, 40U, line);
    (void)snprintf(line, sizeof(line), "BEEP:%s UI:%s",
                   s_buzzer_enabled ? "ON" : "OFF",
                   (s_theme == UI_THEME_NOKIA) ? "NK" : "CL");
    ssd1306_draw_text(2U, 47U, line);
    draw_page_footer(4U);
}

static void render_menu(void)
{
    static const char *items[UI_MENU_ITEM_COUNT] = {
        "DASHBOARD", "CLIMATE", "SYSTEM STATUS", "AIR AND LIGHT",
        "", "", "EXIT", "ABOUT"
    };
    char dynamic_item[20];
    uint8_t first = 0U;

    if (s_menu_index >= UI_CLASSIC_VISIBLE_ITEMS) {
        first = (uint8_t)(s_menu_index - UI_CLASSIC_VISIBLE_ITEMS + 1U);
    }
    ssd1306_draw_text(centered_x("MAIN MENU", 1U), 1U, "MAIN MENU");
    ssd1306_draw_text(110U, 1U, s_fps_label);
    ssd1306_draw_hline(0U, 9U, SSD1306_WIDTH);
    for (uint8_t row = 0U; row < UI_CLASSIC_VISIBLE_ITEMS; ++row) {
        uint8_t i = (uint8_t)(first + row);
        uint8_t y = (uint8_t)(12U + row * 8U);
        const char *label = items[i];
        if (i == 4U) {
            (void)snprintf(dynamic_item, sizeof(dynamic_item), "UI STYLE %s",
                           (s_theme == UI_THEME_NOKIA) ? "NOKIA" : "CLASSIC");
            label = dynamic_item;
        } else if (i == 5U) {
            (void)snprintf(dynamic_item, sizeof(dynamic_item), "BEEP %s",
                           s_buzzer_enabled ? "ON" : "OFF");
            label = dynamic_item;
        }
        if (i == s_menu_index) {
            ssd1306_draw_text(2U, y, ">");
        }
        ssd1306_draw_text(10U, y, label);
    }
}

bool ui_service_init(I2C_HandleTypeDef *i2c, const char *edition)
{
    s_ready = ssd1306_init(i2c);
    if (!s_ready) {
        return false;
    }
    s_edition_tag = ((edition != NULL) && (strstr(edition, "FREE") != NULL)) ?
                    "RT" : "BM";
    s_page = UI_PAGE_DASHBOARD;
    s_menu_open = false;
    s_menu_index = 0U;
    s_buzzer_enabled = true;
    s_theme = (APP_UI_DEFAULT_THEME == 1U) ? UI_THEME_NOKIA : UI_THEME_CLASSIC;
    s_settings_changed = false;
    s_animation_pending = false;
    s_animation_frame = 0U;
    s_fps_counter_started = false;
    s_fps_valid = false;
    s_fps_frame_count = 0U;
    s_fps_window_start_ms = 0U;
    s_fps_label[0] = 'F';
    s_fps_label[1] = '-';
    s_fps_label[2] = '-';
    s_fps_label[3] = '\0';

    ssd1306_clear();
    ssd1306_draw_rect(0U, 0U, SSD1306_WIDTH, SSD1306_HEIGHT);
    ssd1306_draw_text(centered_x("ENV MONITOR", 1U), 5U, "ENV MONITOR");
    ssd1306_draw_hline(8U, 16U, 112U);
    ssd1306_draw_text_scaled(centered_x("STM32", 2U), 21U, "STM32", 2U);
    ssd1306_draw_text(centered_x((edition != NULL) ? edition : "SYSTEM", 1U),
                      41U, (edition != NULL) ? edition : "SYSTEM");
    ssd1306_draw_text(centered_x((s_theme == UI_THEME_NOKIA) ?
                                 "NOKIA READY" : "CLASSIC READY", 1U),
                      53U, (s_theme == UI_THEME_NOKIA) ?
                      "NOKIA READY" : "CLASSIC READY");
    return ssd1306_update();
}

bool ui_service_handle_input(ui_input_t input)
{
    if (s_menu_open) {
        if (input == UI_INPUT_CLOCKWISE) {
            uint8_t previous = s_menu_index;
            s_menu_index = (uint8_t)((s_menu_index + 1U) % UI_MENU_ITEM_COUNT);
            if (s_theme == UI_THEME_NOKIA) {
                s_animation_from = previous;
                s_animation_to = s_menu_index;
                s_animation_frame = 1U;
                s_animation_pending = true;
            }
        } else if (input == UI_INPUT_COUNTERCLOCKWISE) {
            uint8_t previous = s_menu_index;
            s_menu_index = (uint8_t)((s_menu_index + UI_MENU_ITEM_COUNT - 1U) %
                                     UI_MENU_ITEM_COUNT);
            if (s_theme == UI_THEME_NOKIA) {
                s_animation_from = previous;
                s_animation_to = s_menu_index;
                s_animation_frame = 1U;
                s_animation_pending = true;
            }
        } else if (input == UI_INPUT_CLICK) {
            s_animation_pending = false;
            s_animation_frame = 0U;
            if (s_menu_index < 4U) {
                s_page = menu_index_to_page(s_menu_index);
                s_menu_open = false;
            } else if (s_menu_index == 4U) {
                s_theme = (s_theme == UI_THEME_CLASSIC) ?
                          UI_THEME_NOKIA : UI_THEME_CLASSIC;
                s_settings_changed = true;
            } else if (s_menu_index == 5U) {
                s_buzzer_enabled = !s_buzzer_enabled;
                s_settings_changed = true;
            } else if (s_menu_index == 6U) {
                s_menu_open = false;
            } else {
                s_page = UI_PAGE_SYSTEM;
                s_menu_open = false;
            }
        } else if (input == UI_INPUT_LONG_PRESS) {
            s_menu_open = false;
            s_animation_pending = false;
            s_animation_frame = 0U;
        }
        return true;
    }

    if (input == UI_INPUT_CLOCKWISE) {
        s_page = (ui_page_t)(((uint8_t)s_page + 1U) % (uint8_t)UI_PAGE_COUNT);
    } else if (input == UI_INPUT_COUNTERCLOCKWISE) {
        s_page = (ui_page_t)(((uint8_t)s_page + (uint8_t)UI_PAGE_COUNT - 1U) %
                             (uint8_t)UI_PAGE_COUNT);
    } else if (input == UI_INPUT_CLICK) {
        s_menu_open = true;
        s_menu_index = page_to_menu_index(s_page);
        s_animation_pending = false;
        s_animation_frame = 0U;
    } else if (input == UI_INPUT_LONG_PRESS) {
        s_page = UI_PAGE_DASHBOARD;
    }
    return true;
}

bool ui_service_show(const app_snapshot_t *snapshot, bool flash_ok, uint32_t log_count)
{
    if (!s_ready || (snapshot == NULL)) {
        return false;
    }
    if (s_menu_open) {
        if ((s_theme == UI_THEME_NOKIA) && s_animation_pending) {
            if (!ui_theme_nokia_render_animation_frame(s_animation_from,
                                                        s_animation_to,
                                                        s_animation_frame,
                                                        s_theme,
                                                        s_buzzer_enabled,
                                                        s_fps_label)) {
                return false;
            }
            if (s_animation_frame >= APP_UI_ANIMATION_FRAMES) {
                s_animation_pending = false;
                s_animation_frame = 0U;
            } else {
                ++s_animation_frame;
            }
            return true;
        }
        ssd1306_clear();
        if (s_theme == UI_THEME_NOKIA) {
            ui_theme_nokia_render_menu(s_menu_index, s_theme, s_buzzer_enabled,
                                       s_fps_label);
        } else {
            render_menu();
        }
    } else if (s_page == UI_PAGE_CLIMATE) {
        ssd1306_clear();
        render_climate(snapshot);
    } else if (s_page == UI_PAGE_AIR_LIGHT) {
        ssd1306_clear();
        render_air_light(snapshot);
    } else if (s_page == UI_PAGE_SYSTEM) {
        ssd1306_clear();
        render_system(snapshot, flash_ok, log_count);
    } else {
        ssd1306_clear();
        render_dashboard(snapshot, flash_ok, log_count);
    }
    if (!s_menu_open && (s_theme == UI_THEME_NOKIA)) {
        ui_theme_nokia_style_data_page();
    }
    return ssd1306_update();
}

bool ui_service_fps_tick(uint32_t now_ms)
{
    if (!s_fps_counter_started) {
        s_fps_counter_started = true;
        s_fps_window_start_ms = now_ms;
        s_fps_frame_count = 0U;
        return false;
    }

    uint32_t elapsed_ms = now_ms - s_fps_window_start_ms;
    if (elapsed_ms < APP_UI_FPS_SAMPLE_MS) {
        return false;
    }

    uint32_t fps = ((uint32_t)s_fps_frame_count * 1000U +
                    elapsed_ms / 2U) / elapsed_ms;
    if (fps > 99U) {
        fps = 99U;
    }
    char next_label[4];
    (void)snprintf(next_label, sizeof(next_label), "F%02u",
                   (unsigned int)fps);
    bool changed = !s_fps_valid ||
                   (memcmp(next_label, s_fps_label, sizeof(next_label)) != 0);
    memcpy(s_fps_label, next_label, sizeof(s_fps_label));
    s_fps_valid = true;
    s_fps_frame_count = 0U;
    s_fps_window_start_ms = now_ms;
    return changed;
}

void ui_service_record_frame(void)
{
    if (s_fps_counter_started && (s_fps_frame_count < 0xFFFFU)) {
        ++s_fps_frame_count;
    }
}

void ui_service_apply_settings(const ui_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    if ((settings->theme == UI_THEME_CLASSIC) ||
        (settings->theme == UI_THEME_NOKIA)) {
        s_theme = settings->theme;
    }
    s_buzzer_enabled = settings->buzzer_enabled;
    s_settings_changed = false;
    s_animation_pending = false;
    s_animation_frame = 0U;
}

bool ui_service_take_settings_changed(ui_settings_t *settings)
{
    if (!s_settings_changed || (settings == NULL)) {
        return false;
    }
    settings->theme = s_theme;
    settings->buzzer_enabled = s_buzzer_enabled;
    s_settings_changed = false;
    return true;
}

ui_theme_t ui_service_theme(void)
{
    return s_theme;
}

bool ui_service_buzzer_enabled(void)
{
    return s_buzzer_enabled;
}

bool ui_service_animation_active(void)
{
    return s_ready && s_menu_open && (s_theme == UI_THEME_NOKIA) &&
           s_animation_pending;
}
