#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    VALID_RTC    = 1U << 0,
    VALID_SHT30  = 1U << 1,
    VALID_BMP280 = 1U << 2,
    VALID_BH1750 = 1U << 3
};

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static unsigned count_set_bits(uint8_t value)
{
    unsigned count = 0U;
    while (value != 0U) {
        count += (unsigned)(value & 1U);
        value >>= 1U;
    }
    return count;
}

static void print_centi(int32_t centi)
{
    int64_t wide = centi;
    if (wide < 0) {
        putchar('-');
        wide = -wide;
    }
    printf("%" PRId64 ".%02" PRId64, wide / 100, wide % 100);
}

int main(void)
{
    uint8_t valid = (uint8_t)(VALID_RTC | VALID_SHT30 | VALID_BH1750);

    printf("valid mask = 0x%02X, fields = %u\n",
           valid, count_set_bits(valid));
    printf("BMP280 valid? %s\n",
           (valid & VALID_BMP280) != 0U ? "yes" : "no");

    valid |= VALID_BMP280;
    valid &= (uint8_t)~VALID_RTC;
    printf("after update = 0x%02X\n", valid);

    printf("temperature = ");
    print_centi(-534);
    putchar('\n');

    /* Deadline wraps from UINT32_MAX to 0x00000003. */
    const uint32_t start = UINT32_MAX - 2U;
    const uint32_t deadline = start + 6U;
    for (uint32_t elapsed = 0U; elapsed < 9U; ++elapsed) {
        uint32_t now = start + elapsed;
        printf("now=0x%08" PRIX32 " reached=%d\n",
               now, time_reached(now, deadline) ? 1 : 0);
    }
    return 0;
}

