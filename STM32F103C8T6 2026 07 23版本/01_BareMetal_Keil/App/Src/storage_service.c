#include "storage_service.h"
#include <stddef.h>
#include <string.h>
#include "app_config.h"
#include "crc32.h"
#include "w25q64.h"

#define LOG_MAGIC                    0x31474F4CUL
#define LOG_SECTOR_SIZE              4096UL
#define SETTINGS_MAGIC               0x31474643UL
#define SETTINGS_VERSION             1U
#define SETTINGS_SECTOR_SIZE         4096UL
#define SETTINGS_SLOT_STRIDE         16UL
#define SETTINGS_SLOT_COUNT          (SETTINGS_SECTOR_SIZE / SETTINGS_SLOT_STRIDE)
#define STORAGE_SCAN_CHUNK_SIZE      256U
#define STORAGE_PROGRAM_TIMEOUT_MS   25U
#define STORAGE_ERASE_TIMEOUT_MS     3000U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t sequence;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid_mask;
    int16_t temperature_centi;
    uint16_t humidity_centi;
    uint32_t pressure_pa;
    uint32_t illuminance_centi;
    uint32_t crc32;
} log_record_t;

typedef char log_record_size_must_be_32[(sizeof(log_record_t) == 32U) ? 1 : -1];

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t theme;
    uint8_t buzzer_enabled;
    uint8_t reserved;
    uint32_t crc32;
} ui_settings_record_t;

typedef char ui_settings_record_size_must_be_12[
    (sizeof(ui_settings_record_t) == 12U) ? 1 : -1];

typedef enum {
    STORAGE_KIND_NONE = 0,
    STORAGE_KIND_LOG,
    STORAGE_KIND_SETTINGS
} storage_kind_t;

typedef enum {
    STORAGE_PHASE_IDLE = 0,
    STORAGE_PHASE_WAIT_ERASE,
    STORAGE_PHASE_WAIT_PROGRAM
} storage_phase_t;

static bool s_ready;
static uint32_t s_next_slot;
static uint32_t s_next_sequence;
static uint32_t s_valid_records;
static uint32_t s_settings_next_slot;
static bool s_settings_valid;
static ui_settings_t s_cached_settings;

static bool s_log_pending;
static bool s_settings_pending;
static app_snapshot_t s_pending_snapshot;
static ui_settings_t s_pending_settings;

static storage_kind_t s_active_kind;
static storage_phase_t s_phase;
static uint32_t s_active_address;
static uint32_t s_deadline_ms;
static uint16_t s_erased_log_records;
static log_record_t s_active_log;
static ui_settings_record_t s_active_settings;

static uint32_t record_count(void)
{
    return APP_LOG_REGION_SIZE / sizeof(log_record_t);
}

static uint32_t record_address(uint32_t slot)
{
    return APP_LOG_REGION_START + slot * sizeof(log_record_t);
}

static uint32_t settings_address(uint32_t slot)
{
    return APP_SETTINGS_ADDRESS + slot * SETTINGS_SLOT_STRIDE;
}

static bool valid_record(const log_record_t *record)
{
    return (record->magic == LOG_MAGIC) &&
           (record->crc32 == crc32_compute(record, offsetof(log_record_t, crc32)));
}

static bool valid_settings_record(const ui_settings_record_t *record)
{
    return (record->magic == SETTINGS_MAGIC) &&
           (record->version == SETTINGS_VERSION) &&
           (record->theme <= (uint8_t)UI_THEME_NOKIA) &&
           (record->buzzer_enabled <= 1U) &&
           (record->crc32 == crc32_compute(record,
                                            offsetof(ui_settings_record_t, crc32)));
}

static bool bytes_are_erased(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0U; i < length; ++i) {
        if (bytes[i] != 0xFFU) {
            return false;
        }
    }
    return true;
}

static bool sequence_is_newer(uint32_t candidate, uint32_t reference)
{
    return (int32_t)(candidate - reference) > 0;
}

static bool inspect_log_sector(uint32_t address, uint16_t *valid_count,
                               bool *all_erased)
{
    uint8_t scan_buffer[STORAGE_SCAN_CHUNK_SIZE];
    uint32_t sector_address = address & ~(LOG_SECTOR_SIZE - 1UL);
    *valid_count = 0U;
    *all_erased = true;
    for (uint32_t offset = 0U; offset < LOG_SECTOR_SIZE;
         offset += STORAGE_SCAN_CHUNK_SIZE) {
        if (!w25q64_read(sector_address + offset, scan_buffer,
                         sizeof(scan_buffer))) {
            return false;
        }
        for (uint32_t record_offset = 0U;
             record_offset < sizeof(scan_buffer);
             record_offset += sizeof(log_record_t)) {
            log_record_t record;
            memcpy(&record, &scan_buffer[record_offset], sizeof(record));
            if (!bytes_are_erased(&record, sizeof(record))) {
                *all_erased = false;
            }
            if (valid_record(&record)) {
                ++(*valid_count);
            }
        }
    }
    return true;
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static int32_t round_to_i32(float value)
{
    return (int32_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static void build_log_record(log_record_t *record,
                             const app_snapshot_t *snapshot)
{
    memset(record, 0, sizeof(*record));
    record->magic = LOG_MAGIC;
    record->sequence = s_next_sequence;
    record->year = snapshot->time.year;
    record->month = snapshot->time.month;
    record->day = snapshot->time.day;
    record->hour = snapshot->time.hour;
    record->minute = snapshot->time.minute;
    record->second = snapshot->time.second;
    record->valid_mask = snapshot->valid_mask;
    record->temperature_centi =
        (int16_t)round_to_i32(snapshot->temperature_c * 100.0f);
    int32_t humidity = round_to_i32(snapshot->humidity_rh * 100.0f);
    record->humidity_centi = (uint16_t)((humidity < 0) ? 0 :
                                (humidity > 10000 ? 10000 : humidity));
    int32_t pressure = round_to_i32(snapshot->pressure_pa);
    record->pressure_pa = (uint32_t)((pressure < 0) ? 0 : pressure);
    int32_t lux = round_to_i32(snapshot->illuminance_lux * 100.0f);
    record->illuminance_centi = (uint32_t)((lux < 0) ? 0 : lux);
    record->crc32 = crc32_compute(record, offsetof(log_record_t, crc32));
}

static void build_settings_record(ui_settings_record_t *record,
                                  const ui_settings_t *settings)
{
    memset(record, 0, sizeof(*record));
    record->magic = SETTINGS_MAGIC;
    record->version = SETTINGS_VERSION;
    record->theme = (uint8_t)settings->theme;
    record->buzzer_enabled = settings->buzzer_enabled ? 1U : 0U;
    record->reserved = (uint8_t)APP_UI_PROFILE_ID;
    record->crc32 = crc32_compute(record,
                                  offsetof(ui_settings_record_t, crc32));
}

static bool begin_program(uint32_t now_ms)
{
    const void *record;
    size_t length;
    if (s_active_kind == STORAGE_KIND_LOG) {
        record = &s_active_log;
        length = sizeof(s_active_log);
    } else if (s_active_kind == STORAGE_KIND_SETTINGS) {
        record = &s_active_settings;
        length = sizeof(s_active_settings);
    } else {
        return false;
    }
    if (!w25q64_program_begin(s_active_address, record, length)) {
        return false;
    }
    s_phase = STORAGE_PHASE_WAIT_PROGRAM;
    s_deadline_ms = now_ms + STORAGE_PROGRAM_TIMEOUT_MS;
    return true;
}

static bool begin_erase(uint32_t address, uint32_t now_ms)
{
    if (!w25q64_erase_sector_begin(address)) {
        return false;
    }
    s_phase = STORAGE_PHASE_WAIT_ERASE;
    s_deadline_ms = now_ms + STORAGE_ERASE_TIMEOUT_MS;
    return true;
}

static bool start_log_operation(uint32_t now_ms)
{
    log_record_t existing;
    bool sector_erased;
    s_log_pending = false;
    s_active_kind = STORAGE_KIND_LOG;
    s_active_address = record_address(s_next_slot);
    s_erased_log_records = 0U;
    build_log_record(&s_active_log, &s_pending_snapshot);

    uint32_t slot_in_sector =
        (s_active_address & (LOG_SECTOR_SIZE - 1UL)) / sizeof(log_record_t);
    if (slot_in_sector == 0U) {
        if (!inspect_log_sector(s_active_address, &s_erased_log_records,
                                &sector_erased)) {
            return false;
        }
        return sector_erased ? begin_program(now_ms) :
                               begin_erase(s_active_address, now_ms);
    }
    if (!w25q64_read(s_active_address, &existing, sizeof(existing)) ||
        !bytes_are_erased(&existing, sizeof(existing))) {
        return false;
    }
    return begin_program(now_ms);
}

static bool start_settings_operation(uint32_t now_ms)
{
    ui_settings_record_t existing;
    s_settings_pending = false;
    s_active_kind = STORAGE_KIND_SETTINGS;
    build_settings_record(&s_active_settings, &s_pending_settings);

    if (s_settings_next_slot >= SETTINGS_SLOT_COUNT) {
        s_active_address = APP_SETTINGS_ADDRESS;
        return begin_erase(APP_SETTINGS_ADDRESS, now_ms);
    }
    s_active_address = settings_address(s_settings_next_slot);
    if (!w25q64_read(s_active_address, &existing, sizeof(existing)) ||
        !bytes_are_erased(&existing, sizeof(existing))) {
        return false;
    }
    return begin_program(now_ms);
}

static storage_process_result_t fail_operation(void)
{
    s_ready = false;
    s_log_pending = false;
    s_settings_pending = false;
    s_active_kind = STORAGE_KIND_NONE;
    s_phase = STORAGE_PHASE_IDLE;
    s_erased_log_records = 0U;
    return STORAGE_PROCESS_ERROR;
}

static storage_process_result_t finish_program(void)
{
    if (s_active_kind == STORAGE_KIND_LOG) {
        log_record_t verify;
        if (!w25q64_read(s_active_address, &verify, sizeof(verify)) ||
            !valid_record(&verify) ||
            (verify.sequence != s_active_log.sequence)) {
            return fail_operation();
        }
        s_next_slot = (s_next_slot + 1U) % record_count();
        ++s_next_sequence;
        if (s_valid_records < record_count()) {
            ++s_valid_records;
        }
        s_active_kind = STORAGE_KIND_NONE;
        s_phase = STORAGE_PHASE_IDLE;
        return STORAGE_PROCESS_APPEND_DONE;
    }

    if (s_active_kind == STORAGE_KIND_SETTINGS) {
        ui_settings_record_t verify;
        if (!w25q64_read(s_active_address, &verify, sizeof(verify)) ||
            !valid_settings_record(&verify) ||
            (memcmp(&verify, &s_active_settings, sizeof(verify)) != 0)) {
            return fail_operation();
        }
        s_cached_settings.theme = (ui_theme_t)verify.theme;
        s_cached_settings.buzzer_enabled = verify.buzzer_enabled != 0U;
        s_settings_valid = true;
        ++s_settings_next_slot;
        s_active_kind = STORAGE_KIND_NONE;
        s_phase = STORAGE_PHASE_IDLE;
        return STORAGE_PROCESS_SETTINGS_DONE;
    }
    return fail_operation();
}

bool storage_service_init(SPI_HandleTypeDef *spi, uint32_t *jedec_id)
{
    uint8_t scan_buffer[STORAGE_SCAN_CHUNK_SIZE];
    bool flash_ready;
    bool have_latest = false;
    uint32_t latest_slot = 0U;
    uint32_t latest_sequence = 0U;

    s_ready = false;
    s_next_slot = 0U;
    s_next_sequence = 1U;
    s_valid_records = 0U;
    s_settings_next_slot = 0U;
    s_settings_valid = false;
    s_log_pending = false;
    s_settings_pending = false;
    s_active_kind = STORAGE_KIND_NONE;
    s_phase = STORAGE_PHASE_IDLE;
    s_erased_log_records = 0U;

    if (!w25q64_init(spi, jedec_id) ||
        !w25q64_is_ready(&flash_ready) || !flash_ready) {
        return false;
    }

    const uint32_t records_per_scan =
        STORAGE_SCAN_CHUNK_SIZE / sizeof(log_record_t);
    for (uint32_t base = 0U; base < record_count(); base += records_per_scan) {
        uint32_t batch = record_count() - base;
        if (batch > records_per_scan) {
            batch = records_per_scan;
        }
        size_t length = (size_t)batch * sizeof(log_record_t);
        if (!w25q64_read(record_address(base), scan_buffer, length)) {
            return false;
        }
        for (uint32_t i = 0U; i < batch; ++i) {
            log_record_t record;
            memcpy(&record, &scan_buffer[i * sizeof(record)], sizeof(record));
            if (!valid_record(&record)) {
                continue;
            }
            ++s_valid_records;
            if (!have_latest || sequence_is_newer(record.sequence,
                                                   latest_sequence)) {
                have_latest = true;
                latest_sequence = record.sequence;
                latest_slot = base + i;
            }
        }
    }
    if (have_latest) {
        s_next_slot = (latest_slot + 1U) % record_count();
        s_next_sequence = latest_sequence + 1U;
    }

    for (uint32_t offset = 0U; offset < SETTINGS_SECTOR_SIZE;
         offset += STORAGE_SCAN_CHUNK_SIZE) {
        if (!w25q64_read(APP_SETTINGS_ADDRESS + offset, scan_buffer,
                         sizeof(scan_buffer))) {
            return false;
        }
        for (uint32_t i = 0U;
             i < (STORAGE_SCAN_CHUNK_SIZE / SETTINGS_SLOT_STRIDE); ++i) {
            uint32_t slot = (offset / SETTINGS_SLOT_STRIDE) + i;
            ui_settings_record_t record;
            memcpy(&record, &scan_buffer[i * SETTINGS_SLOT_STRIDE],
                   sizeof(record));
            if (!bytes_are_erased(&record, sizeof(record))) {
                s_settings_next_slot = slot + 1U;
            }
            if (valid_settings_record(&record) &&
                (record.reserved == (uint8_t)APP_UI_PROFILE_ID)) {
                s_cached_settings.theme = (ui_theme_t)record.theme;
                s_cached_settings.buzzer_enabled =
                    record.buzzer_enabled != 0U;
                s_settings_valid = true;
            }
        }
    }
    s_ready = true;
    return true;
}

bool storage_service_load_ui_settings(ui_settings_t *settings)
{
    if (!s_ready || !s_settings_valid || (settings == NULL)) {
        return false;
    }
    *settings = s_cached_settings;
    return true;
}

bool storage_service_queue_append(const app_snapshot_t *snapshot)
{
    if (!s_ready || (snapshot == NULL)) {
        return false;
    }
    s_pending_snapshot = *snapshot;
    s_log_pending = true;
    return true;
}

bool storage_service_queue_ui_settings(const ui_settings_t *settings)
{
    if (!s_ready || (settings == NULL) ||
        ((settings->theme != UI_THEME_CLASSIC) &&
         (settings->theme != UI_THEME_NOKIA))) {
        return false;
    }
    s_pending_settings = *settings;
    s_settings_pending = true;
    return true;
}

storage_process_result_t storage_service_process(uint32_t now_ms)
{
    if (!s_ready) {
        return STORAGE_PROCESS_ERROR;
    }
    if (s_phase == STORAGE_PHASE_IDLE) {
        bool started = true;
        if (s_log_pending) {
            started = start_log_operation(now_ms);
        } else if (s_settings_pending) {
            started = start_settings_operation(now_ms);
        } else {
            return STORAGE_PROCESS_IDLE;
        }
        return started ? STORAGE_PROCESS_IDLE : fail_operation();
    }

    bool flash_ready;
    if (!w25q64_is_ready(&flash_ready)) {
        return fail_operation();
    }
    if (!flash_ready) {
        return time_reached(now_ms, s_deadline_ms) ?
               fail_operation() : STORAGE_PROCESS_IDLE;
    }

    if (s_phase == STORAGE_PHASE_WAIT_ERASE) {
        if (s_active_kind == STORAGE_KIND_SETTINGS) {
            s_settings_next_slot = 0U;
            s_active_address = APP_SETTINGS_ADDRESS;
        } else if (s_active_kind == STORAGE_KIND_LOG) {
            if (s_valid_records >= s_erased_log_records) {
                s_valid_records -= s_erased_log_records;
            } else {
                s_valid_records = 0U;
            }
            s_erased_log_records = 0U;
        }
        return begin_program(now_ms) ? STORAGE_PROCESS_IDLE : fail_operation();
    }
    if (s_phase == STORAGE_PHASE_WAIT_PROGRAM) {
        return finish_program();
    }
    return fail_operation();
}

bool storage_service_busy(void)
{
    return s_phase != STORAGE_PHASE_IDLE || s_log_pending || s_settings_pending;
}

uint32_t storage_service_record_count(void)
{
    return s_valid_records;
}
