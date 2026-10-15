#include "ssd1306.h"
#include <string.h>
#include "app_config.h"

static I2C_HandleTypeDef *s_i2c;
static uint8_t s_address;
static uint8_t s_buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8U];
static uint8_t s_sent_buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8U];
static bool s_force_full_update;
static ssd1306_stats_t s_stats;

static const uint8_t s_digits[10][5] = {
    {0x3EU,0x51U,0x49U,0x45U,0x3EU}, {0x00U,0x42U,0x7FU,0x40U,0x00U},
    {0x42U,0x61U,0x51U,0x49U,0x46U}, {0x21U,0x41U,0x45U,0x4BU,0x31U},
    {0x18U,0x14U,0x12U,0x7FU,0x10U}, {0x27U,0x45U,0x45U,0x45U,0x39U},
    {0x3CU,0x4AU,0x49U,0x49U,0x30U}, {0x01U,0x71U,0x09U,0x05U,0x03U},
    {0x36U,0x49U,0x49U,0x49U,0x36U}, {0x06U,0x49U,0x49U,0x29U,0x1EU}
};

static const uint8_t s_letters[26][5] = {
    {0x7EU,0x11U,0x11U,0x11U,0x7EU}, {0x7FU,0x49U,0x49U,0x49U,0x36U},
    {0x3EU,0x41U,0x41U,0x41U,0x22U}, {0x7FU,0x41U,0x41U,0x22U,0x1CU},
    {0x7FU,0x49U,0x49U,0x49U,0x41U}, {0x7FU,0x09U,0x09U,0x09U,0x01U},
    {0x3EU,0x41U,0x49U,0x49U,0x7AU}, {0x7FU,0x08U,0x08U,0x08U,0x7FU},
    {0x00U,0x41U,0x7FU,0x41U,0x00U}, {0x20U,0x40U,0x41U,0x3FU,0x01U},
    {0x7FU,0x08U,0x14U,0x22U,0x41U}, {0x7FU,0x40U,0x40U,0x40U,0x40U},
    {0x7FU,0x02U,0x0CU,0x02U,0x7FU}, {0x7FU,0x04U,0x08U,0x10U,0x7FU},
    {0x3EU,0x41U,0x41U,0x41U,0x3EU}, {0x7FU,0x09U,0x09U,0x09U,0x06U},
    {0x3EU,0x41U,0x51U,0x21U,0x5EU}, {0x7FU,0x09U,0x19U,0x29U,0x46U},
    {0x46U,0x49U,0x49U,0x49U,0x31U}, {0x01U,0x01U,0x7FU,0x01U,0x01U},
    {0x3FU,0x40U,0x40U,0x40U,0x3FU}, {0x1FU,0x20U,0x40U,0x20U,0x1FU},
    {0x3FU,0x40U,0x38U,0x40U,0x3FU}, {0x63U,0x14U,0x08U,0x14U,0x63U},
    {0x07U,0x08U,0x70U,0x08U,0x07U}, {0x61U,0x51U,0x49U,0x45U,0x43U}
};

static void glyph(char character, uint8_t columns[5])
{
    memset(columns, 0, 5U);
    if ((character >= '0') && (character <= '9')) {
        memcpy(columns, s_digits[(uint8_t)(character - '0')], 5U);
    } else if ((character >= 'A') && (character <= 'Z')) {
        memcpy(columns, s_letters[(uint8_t)(character - 'A')], 5U);
    } else if ((character >= 'a') && (character <= 'z')) {
        memcpy(columns, s_letters[(uint8_t)(character - 'a')], 5U);
    } else {
        switch (character) {
        case '-': columns[0]=0x08U; columns[1]=0x08U; columns[2]=0x08U; columns[3]=0x08U; columns[4]=0x08U; break;
        case ':': columns[1]=0x36U; columns[3]=0x36U; break;
        case '.': columns[1]=0x60U; columns[2]=0x60U; break;
        case '/': columns[0]=0x20U; columns[1]=0x10U; columns[2]=0x08U; columns[3]=0x04U; columns[4]=0x02U; break;
        case '%': columns[0]=0x62U; columns[1]=0x64U; columns[2]=0x08U; columns[3]=0x13U; columns[4]=0x23U; break;
        case '?': columns[0]=0x02U; columns[1]=0x01U; columns[2]=0x51U; columns[3]=0x09U; columns[4]=0x06U; break;
        case '>': columns[0]=0x00U; columns[1]=0x41U; columns[2]=0x22U; columns[3]=0x14U; columns[4]=0x08U; break;
        case '<': columns[0]=0x08U; columns[1]=0x14U; columns[2]=0x22U; columns[3]=0x41U; columns[4]=0x00U; break;
        case '!': columns[2]=0x5FU; break;
        case '_': columns[0]=0x40U; columns[1]=0x40U; columns[2]=0x40U; columns[3]=0x40U; columns[4]=0x40U; break;
        default: break;
        }
    }
}

static bool command_sequence(const uint8_t *values, uint16_t length)
{
    uint8_t packet[32];
    if ((values == NULL) || (length == 0U) ||
        (length > (uint16_t)(sizeof(packet) - 1U))) {
        return false;
    }
    packet[0] = 0x00U;
    memcpy(&packet[1], values, length);
    return HAL_I2C_Master_Transmit(s_i2c, s_address << 1U, packet,
                                   (uint16_t)(length + 1U),
                                   APP_I2C_TIMEOUT_MS) == HAL_OK;
}

bool ssd1306_init(I2C_HandleTypeDef *i2c)
{
    static const uint8_t sequence[] = {
        0xAEU, 0x20U, 0x02U, 0xB0U, 0xC8U, 0x00U, 0x10U, 0x40U,
        0x81U, 0x7FU, 0xA1U, 0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U,
        0x00U, 0xD5U, 0x80U, 0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU,
        0x40U, 0x8DU, 0x14U, 0xAFU
    };
    s_i2c = i2c;
    if (s_i2c == NULL) {
        return false;
    }
    if (HAL_I2C_IsDeviceReady(s_i2c, APP_OLED_ADDRESS_PRIMARY << 1U, 3U,
                              APP_I2C_TIMEOUT_MS) == HAL_OK) {
        s_address = APP_OLED_ADDRESS_PRIMARY;
    } else if (HAL_I2C_IsDeviceReady(s_i2c, APP_OLED_ADDRESS_SECONDARY << 1U, 3U,
                                     APP_I2C_TIMEOUT_MS) == HAL_OK) {
        s_address = APP_OLED_ADDRESS_SECONDARY;
    } else {
        return false;
    }
    if (!command_sequence(sequence, sizeof(sequence))) {
        return false;
    }
    memset(s_sent_buffer, 0, sizeof(s_sent_buffer));
    memset(&s_stats, 0, sizeof(s_stats));
    s_force_full_update = true;
    ssd1306_clear();
    return ssd1306_update();
}

void ssd1306_clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on)
{
    if ((x >= SSD1306_WIDTH) || (y >= SSD1306_HEIGHT)) {
        return;
    }
    uint16_t index = (uint16_t)x + (uint16_t)(y / 8U) * SSD1306_WIDTH;
    uint8_t mask = (uint8_t)(1U << (y & 7U));
    if (on) {
        s_buffer[index] |= mask;
    } else {
        s_buffer[index] &= (uint8_t)~mask;
    }
}

void ssd1306_draw_hline(uint8_t x, uint8_t y, uint8_t width)
{
    for (uint16_t offset = 0U; offset < width; ++offset) {
        uint16_t px = (uint16_t)x + offset;
        if (px >= SSD1306_WIDTH) {
            break;
        }
        ssd1306_draw_pixel((uint8_t)px, y, true);
    }
}

void ssd1306_draw_vline(uint8_t x, uint8_t y, uint8_t height)
{
    for (uint16_t offset = 0U; offset < height; ++offset) {
        uint16_t py = (uint16_t)y + offset;
        if (py >= SSD1306_HEIGHT) {
            break;
        }
        ssd1306_draw_pixel(x, (uint8_t)py, true);
    }
}

void ssd1306_draw_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    if ((width == 0U) || (height == 0U)) {
        return;
    }
    ssd1306_draw_hline(x, y, width);
    ssd1306_draw_hline(x, (uint8_t)(y + height - 1U), width);
    ssd1306_draw_vline(x, y, height);
    ssd1306_draw_vline((uint8_t)(x + width - 1U), y, height);
}

void ssd1306_invert_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    uint16_t x_end = (uint16_t)x + width;
    uint16_t y_end = (uint16_t)y + height;
    if ((width == 0U) || (height == 0U) || (x >= SSD1306_WIDTH) ||
        (y >= SSD1306_HEIGHT)) {
        return;
    }
    if (x_end > SSD1306_WIDTH) {
        x_end = SSD1306_WIDTH;
    }
    if (y_end > SSD1306_HEIGHT) {
        y_end = SSD1306_HEIGHT;
    }

    uint8_t first_page = (uint8_t)(y / 8U);
    uint8_t last_page = (uint8_t)((y_end - 1U) / 8U);
    for (uint8_t page = first_page; page <= last_page; ++page) {
        uint16_t page_y = (uint16_t)page * 8U;
        uint8_t first_bit = (y > page_y) ? (uint8_t)(y - page_y) : 0U;
        uint8_t last_bit = (y_end < (page_y + 8U)) ?
                           (uint8_t)(y_end - page_y) : 8U;
        uint16_t high_mask = (last_bit == 8U) ? 0x00FFU :
                             (uint16_t)((1U << last_bit) - 1U);
        uint16_t low_mask = (first_bit == 0U) ? 0U :
                            (uint16_t)((1U << first_bit) - 1U);
        uint8_t mask = (uint8_t)(high_mask & (uint16_t)~low_mask);
        uint16_t offset = (uint16_t)page * SSD1306_WIDTH;
        for (uint16_t px = x; px < x_end; ++px) {
            s_buffer[offset + px] ^= mask;
        }
    }
}

void ssd1306_draw_char(uint8_t x, uint8_t y, char character)
{
    if ((x + 5U >= SSD1306_WIDTH) || (y + 7U >= SSD1306_HEIGHT)) {
        return;
    }
    uint8_t columns[5];
    glyph(character, columns);
    for (uint8_t column = 0U; column < 5U; ++column) {
        for (uint8_t row = 0U; row < 7U; ++row) {
            uint16_t index = (uint16_t)(x + column) +
                             (uint16_t)((y + row) / 8U) * SSD1306_WIDTH;
            uint8_t mask = (uint8_t)(1U << ((y + row) & 7U));
            if ((columns[column] & (1U << row)) != 0U) {
                s_buffer[index] |= mask;
            } else {
                s_buffer[index] &= (uint8_t)~mask;
            }
        }
    }
}

void ssd1306_draw_text(uint8_t x, uint8_t y, const char *text)
{
    if (text == NULL) {
        return;
    }
    while ((*text != '\0') && (x + 5U < SSD1306_WIDTH)) {
        ssd1306_draw_char(x, y, *text++);
        x = (uint8_t)(x + 6U);
    }
}

void ssd1306_draw_text_scaled(uint8_t x, uint8_t y, const char *text, uint8_t scale)
{
    if ((text == NULL) || (scale == 0U)) {
        return;
    }
    while (*text != '\0') {
        uint16_t character_width = (uint16_t)5U * scale;
        uint16_t advance = (uint16_t)6U * scale;
        if (((uint16_t)x + character_width > SSD1306_WIDTH) ||
            ((uint16_t)y + (uint16_t)7U * scale > SSD1306_HEIGHT)) {
            break;
        }
        uint8_t columns[5];
        glyph(*text++, columns);
        for (uint8_t column = 0U; column < 5U; ++column) {
            for (uint8_t row = 0U; row < 7U; ++row) {
                if ((columns[column] & (1U << row)) == 0U) {
                    continue;
                }
                for (uint8_t dx = 0U; dx < scale; ++dx) {
                    for (uint8_t dy = 0U; dy < scale; ++dy) {
                        ssd1306_draw_pixel((uint8_t)((uint16_t)x +
                                                   (uint16_t)column * scale + dx),
                                           (uint8_t)((uint16_t)y +
                                                   (uint16_t)row * scale + dy),
                                           true);
                    }
                }
            }
        }
        if ((uint16_t)x + advance >= SSD1306_WIDTH) {
            break;
        }
        x = (uint8_t)((uint16_t)x + advance);
    }
}

static bool transmit_run(uint8_t page, uint8_t first_column,
                         uint8_t last_column, uint8_t *packet)
{
    uint8_t address_commands[3] = {
        (uint8_t)(0xB0U + page),
        (uint8_t)(first_column & 0x0FU),
        (uint8_t)(0x10U | (first_column >> 4U))
    };
    if (!command_sequence(address_commands, sizeof(address_commands))) {
        return false;
    }

    uint16_t offset = (uint16_t)page * SSD1306_WIDTH;
    uint16_t length = (uint16_t)last_column - first_column + 1U;
    memcpy(&packet[1], &s_buffer[offset + first_column], length);
    if (HAL_I2C_Master_Transmit(s_i2c, s_address << 1U, packet,
                                (uint16_t)(length + 1U),
                                APP_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }
    memcpy(&s_sent_buffer[offset + first_column],
           &s_buffer[offset + first_column], length);
    ++s_stats.transmitted_runs;
    s_stats.pixel_bytes += length;
    return true;
}

bool ssd1306_update(void)
{
    uint8_t packet[SSD1306_WIDTH + 1U];
    packet[0] = 0x40U;
    ++s_stats.update_calls;
    for (uint8_t page = 0U; page < (SSD1306_HEIGHT / 8U); ++page) {
        uint16_t offset = (uint16_t)page * SSD1306_WIDTH;
        if (s_force_full_update) {
            if (!transmit_run(page, 0U, SSD1306_WIDTH - 1U, packet)) {
                return false;
            }
            continue;
        }

        uint8_t cursor = 0U;
        while (cursor < SSD1306_WIDTH) {
            while ((cursor < SSD1306_WIDTH) &&
                   (s_buffer[offset + cursor] == s_sent_buffer[offset + cursor])) {
                ++cursor;
            }
            if (cursor >= SSD1306_WIDTH) {
                break;
            }

            uint8_t first_column = cursor;
            uint8_t last_column = cursor;
            uint8_t clean_gap = 0U;
            ++cursor;
            while (cursor < SSD1306_WIDTH) {
                if (s_buffer[offset + cursor] != s_sent_buffer[offset + cursor]) {
                    last_column = cursor;
                    clean_gap = 0U;
                } else {
                    ++clean_gap;
                    if (clean_gap > APP_OLED_DIRTY_MERGE_GAP) {
                        break;
                    }
                }
                ++cursor;
            }
            if (!transmit_run(page, first_column, last_column, packet)) {
                return false;
            }
        }
    }
    s_force_full_update = false;
    return true;
}

uint8_t ssd1306_address(void)
{
    return s_address;
}

void ssd1306_get_stats(ssd1306_stats_t *stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
