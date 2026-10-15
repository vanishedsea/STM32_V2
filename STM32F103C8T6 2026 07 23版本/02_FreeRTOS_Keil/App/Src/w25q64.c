#include "w25q64.h"
#include <string.h>
#include "app_config.h"

#define W25Q64_CMD_JEDEC_ID        0x9FU
#define W25Q64_CMD_READ            0x03U
#define W25Q64_CMD_PAGE_PROGRAM    0x02U
#define W25Q64_CMD_WRITE_ENABLE    0x06U
#define W25Q64_CMD_READ_STATUS1    0x05U
#define W25Q64_CMD_SECTOR_ERASE    0x20U
#define W25Q64_CAPACITY_BYTES      0x00800000UL

static SPI_HandleTypeDef *s_spi;

static void cs_low(void) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET); }

static bool transmit(const uint8_t *data, uint16_t length)
{
    return HAL_SPI_Transmit(s_spi, (uint8_t *)data, length, APP_SPI_TIMEOUT_MS) == HAL_OK;
}

static bool receive(uint8_t *data, uint16_t length)
{
    return HAL_SPI_Receive(s_spi, data, length, APP_SPI_TIMEOUT_MS) == HAL_OK;
}

static bool write_enable(void)
{
    uint8_t command = W25Q64_CMD_WRITE_ENABLE;
    cs_low();
    bool ok = transmit(&command, 1U);
    cs_high();
    return ok;
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    do {
        bool ready;
        if (!w25q64_is_ready(&ready)) {
            return false;
        }
        if (ready) {
            return true;
        }
    } while ((HAL_GetTick() - start) < timeout_ms);
    return false;
}

bool w25q64_init(SPI_HandleTypeDef *spi, uint32_t *jedec_id)
{
    uint8_t command = W25Q64_CMD_JEDEC_ID;
    uint8_t id[3] = {0};
    s_spi = spi;
    if (s_spi == NULL) {
        return false;
    }
    cs_high();
    cs_low();
    bool ok = transmit(&command, 1U) && receive(id, sizeof(id));
    cs_high();
    uint32_t value = ((uint32_t)id[0] << 16U) | ((uint32_t)id[1] << 8U) | id[2];
    if (jedec_id != NULL) {
        *jedec_id = value;
    }
    /* W25Q64 is normally EF 40 17. Accept compatible 64-Mbit parts with capacity code 0x17. */
    return ok && (id[0] != 0x00U) && (id[0] != 0xFFU) && (id[2] == 0x17U);
}

bool w25q64_read(uint32_t address, void *data, size_t length)
{
    if ((s_spi == NULL) || (data == NULL) || (length == 0U) ||
        (address + length > W25Q64_CAPACITY_BYTES)) {
        return false;
    }
    uint8_t header[4] = {
        W25Q64_CMD_READ,
        (uint8_t)(address >> 16U),
        (uint8_t)(address >> 8U),
        (uint8_t)address
    };
    cs_low();
    bool ok = transmit(header, sizeof(header));
    uint8_t *output = (uint8_t *)data;
    while (ok && (length > 0U)) {
        uint16_t chunk = (length > 65535U) ? 65535U : (uint16_t)length;
        ok = receive(output, chunk);
        output += chunk;
        length -= chunk;
    }
    cs_high();
    return ok;
}

bool w25q64_is_ready(bool *ready)
{
    uint8_t command = W25Q64_CMD_READ_STATUS1;
    uint8_t status = 0xFFU;
    if ((s_spi == NULL) || (ready == NULL)) {
        return false;
    }
    cs_low();
    bool ok = transmit(&command, 1U) && receive(&status, 1U);
    cs_high();
    if (ok) {
        *ready = (status & 0x01U) == 0U;
    }
    return ok;
}

bool w25q64_program_begin(uint32_t address, const void *data, size_t length)
{
    if ((s_spi == NULL) || (data == NULL) || (length == 0U) ||
        (length > 256U) || (address + length > W25Q64_CAPACITY_BYTES) ||
        (((address & 0xFFU) + length) > 256U)) {
        return false;
    }
    uint8_t header[4] = {
        W25Q64_CMD_PAGE_PROGRAM,
        (uint8_t)(address >> 16U),
        (uint8_t)(address >> 8U),
        (uint8_t)address
    };
    if (!write_enable()) {
        return false;
    }
    cs_low();
    bool ok = transmit(header, sizeof(header)) &&
              transmit((const uint8_t *)data, (uint16_t)length);
    cs_high();
    return ok;
}

bool w25q64_erase_sector_begin(uint32_t address)
{
    if ((s_spi == NULL) || (address >= W25Q64_CAPACITY_BYTES)) {
        return false;
    }
    address &= ~0xFFFUL;
    uint8_t command[4] = {
        W25Q64_CMD_SECTOR_ERASE,
        (uint8_t)(address >> 16U),
        (uint8_t)(address >> 8U),
        (uint8_t)address
    };
    if (!write_enable()) {
        return false;
    }
    cs_low();
    bool ok = transmit(command, sizeof(command));
    cs_high();
    return ok;
}

bool w25q64_program(uint32_t address, const void *data, size_t length)
{
    if ((s_spi == NULL) || (data == NULL) || (length == 0U) ||
        (address + length > W25Q64_CAPACITY_BYTES)) {
        return false;
    }
    const uint8_t *input = (const uint8_t *)data;
    while (length > 0U) {
        uint16_t page_space = (uint16_t)(256U - (address & 0xFFU));
        uint16_t chunk = (length < page_space) ? (uint16_t)length : page_space;
        if (!w25q64_program_begin(address, input, chunk) || !wait_ready(20U)) {
            return false;
        }
        address += chunk;
        input += chunk;
        length -= chunk;
    }
    return true;
}

bool w25q64_erase_sector(uint32_t address)
{
    return w25q64_erase_sector_begin(address) && wait_ready(3000U);
}
