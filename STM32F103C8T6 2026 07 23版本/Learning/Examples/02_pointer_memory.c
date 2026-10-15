#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OLED_WIDTH  128U
#define OLED_HEIGHT 64U
#define OLED_BYTES  (OLED_WIDTH * OLED_HEIGHT / 8U)

static bool set_pixel(uint8_t *buffer, size_t length,
                      uint8_t x, uint8_t y, bool on)
{
    if ((buffer == NULL) || (length < OLED_BYTES) ||
        (x >= OLED_WIDTH) || (y >= OLED_HEIGHT)) {
        return false;
    }

    size_t index = ((size_t)y / 8U) * OLED_WIDTH + x;
    uint8_t mask = (uint8_t)(1U << (y & 7U));
    if (on) {
        buffer[index] |= mask;
    } else {
        buffer[index] &= (uint8_t)~mask;
    }
    return true;
}

static bool pixel_is_on(const uint8_t *buffer, size_t length,
                        uint8_t x, uint8_t y)
{
    if ((buffer == NULL) || (length < OLED_BYTES) ||
        (x >= OLED_WIDTH) || (y >= OLED_HEIGHT)) {
        return false;
    }
    size_t index = ((size_t)y / 8U) * OLED_WIDTH + x;
    uint8_t mask = (uint8_t)(1U << (y & 7U));
    return (buffer[index] & mask) != 0U;
}

int main(void)
{
    uint8_t framebuffer[OLED_BYTES];
    memset(framebuffer, 0, sizeof(framebuffer));

    assert(set_pixel(framebuffer, sizeof(framebuffer), 10U, 9U, true));
    assert(pixel_is_on(framebuffer, sizeof(framebuffer), 10U, 9U));

    size_t index = (9U / 8U) * OLED_WIDTH + 10U;
    printf("pixel(10,9): index=%zu byte=0x%02X\n",
           index, framebuffer[index]);

    assert(set_pixel(framebuffer, sizeof(framebuffer), 10U, 9U, false));
    assert(!pixel_is_on(framebuffer, sizeof(framebuffer), 10U, 9U));
    assert(!set_pixel(framebuffer, sizeof(framebuffer), 128U, 0U, true));
    assert(!set_pixel(NULL, 0U, 0U, 0U, true));

    puts("pointer/memory tests passed");
    return 0;
}

