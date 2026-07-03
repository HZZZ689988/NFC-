#include "att_storage.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "att_crc16.h"
#include "att_crc32.h"
#include "att_ota_format.h"
#include "lfs.h"

#define ATT_CFG_MAGIC       0x41544346u
#define ATT_CFG_VERSION     1u
#define ATT_CFG_PATH        "config.bin"
#define ATT_RECORD_PATH     "records.bin"
#define ATT_RECORD_META_MAGIC   0x4154524Du
#define ATT_RECORD_META_VERSION 1u
#define ATT_RECORD_META_PATH    "records.meta"
#define ATT_WEATHER_PATH    "weather.txt"

#ifndef ATT_STORAGE_MAX_RECORDS
#define ATT_STORAGE_MAX_RECORDS 1024u
#endif

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    att_device_config_t config;
    uint16_t crc16;
} att_config_file_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t max_records;
    uint32_t head;
    uint32_t count;
    uint16_t crc16;
} att_record_meta_t;

extern const struct lfs_config g_att_lfs_cfg;

static lfs_t s_lfs;
static uint8_t s_mounted;

static att_status_t ensure_mounted(void)
{
    return s_mounted ? ATT_OK : ATT_ERR_NOT_READY;
}

static uint16_t record_crc(const att_record_t *record)
{
    return att_crc16_ccitt_false(record, offsetof(att_record_t, crc16));
}

static void record_meta_init(att_record_meta_t *meta, uint32_t count)
{
    memset(meta, 0, sizeof(*meta));
    meta->magic = ATT_RECORD_META_MAGIC;
    meta->version = ATT_RECORD_META_VERSION;
    meta->size = sizeof(att_record_meta_t);
    meta->max_records = ATT_STORAGE_MAX_RECORDS;
    meta->head = 0u;
    meta->count = count > ATT_STORAGE_MAX_RECORDS ? ATT_STORAGE_MAX_RECORDS : count;
}

static att_status_t save_record_meta(att_record_meta_t *meta)
{
    if (meta == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    meta->crc16 = 0u;
    meta->crc16 = att_crc16_ccitt_false(meta, offsetof(att_record_meta_t, crc16));

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_META_PATH,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return ATT_ERR_STORAGE;
    }
    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, meta, sizeof(*meta));
    int close_err = lfs_file_close(&s_lfs, &file);

    return (written == (lfs_ssize_t)sizeof(*meta) && close_err == 0) ? ATT_OK : ATT_ERR_STORAGE;
}

static att_status_t load_record_meta(att_record_meta_t *meta)
{
    if (meta == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_META_PATH, LFS_O_RDONLY) == 0) {
        att_record_meta_t stored;
        lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, &stored, sizeof(stored));
        (void)lfs_file_close(&s_lfs, &file);
        if (read_len == (lfs_ssize_t)sizeof(stored)) {
            uint16_t expected = stored.crc16;
            stored.crc16 = 0u;
            uint16_t actual = att_crc16_ccitt_false(&stored, offsetof(att_record_meta_t, crc16));
            stored.crc16 = expected;
            if (stored.magic == ATT_RECORD_META_MAGIC &&
                stored.version == ATT_RECORD_META_VERSION &&
                stored.size == sizeof(att_record_meta_t) &&
                stored.max_records == ATT_STORAGE_MAX_RECORDS &&
                stored.head < ATT_STORAGE_MAX_RECORDS &&
                stored.count <= ATT_STORAGE_MAX_RECORDS &&
                expected == actual) {
                *meta = stored;
                return ATT_OK;
            }
        }
    }

    uint32_t legacy_count = 0u;
    struct lfs_info info;
    if (lfs_stat(&s_lfs, ATT_RECORD_PATH, &info) == 0) {
        legacy_count = (uint32_t)(info.size / sizeof(att_record_t));
    }
    record_meta_init(meta, legacy_count);
    return save_record_meta(meta);
}

static uint16_t ota_meta_crc(const att_ota_meta_t *meta)
{
    return att_crc16_ccitt_false(meta, offsetof(att_ota_meta_t, crc16));
}

static uint16_t boot_state_crc(const att_boot_state_t *state)
{
    return att_crc16_ccitt_false(state, offsetof(att_boot_state_t, crc16));
}

static void boot_state_init(att_boot_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->magic = ATT_BOOT_STATE_MAGIC;
    state->version = ATT_BOOT_STATE_VERSION;
    state->size = sizeof(att_boot_state_t);
    state->active_slot = ATT_APP_SLOT_ID;
    state->confirmed_slot = ATT_APP_SLOT_ID;
}

static att_status_t save_boot_state(att_boot_state_t *state)
{
    if (state == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    state->crc16 = 0u;
    state->crc16 = boot_state_crc(state);

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_BOOT_STATE_PATH,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return ATT_ERR_STORAGE;
    }

    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, state, sizeof(*state));
    int close_err = lfs_file_close(&s_lfs, &file);
    return (written == (lfs_ssize_t)sizeof(*state) && close_err == 0) ? ATT_OK : ATT_ERR_STORAGE;
}

static att_status_t load_boot_state(att_boot_state_t *state)
{
    if (state == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_BOOT_STATE_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    att_boot_state_t stored;
    lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, &stored, sizeof(stored));
    (void)lfs_file_close(&s_lfs, &file);
    if (read_len != (lfs_ssize_t)sizeof(stored)) {
        return ATT_ERR_STORAGE;
    }

    uint16_t expected = stored.crc16;
    stored.crc16 = 0u;
    uint16_t actual = boot_state_crc(&stored);
    stored.crc16 = expected;
    if (stored.magic != ATT_BOOT_STATE_MAGIC ||
        stored.version != ATT_BOOT_STATE_VERSION ||
        stored.size != sizeof(att_boot_state_t) ||
        expected != actual) {
        return ATT_ERR_CRC;
    }

    *state = stored;
    return ATT_OK;
}

static att_status_t save_ota_meta(att_ota_meta_t *meta)
{
    if (meta == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    meta->crc16 = 0u;
    meta->crc16 = ota_meta_crc(meta);

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_META_PATH,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return ATT_ERR_STORAGE;
    }
    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, meta, sizeof(*meta));
    int close_err = lfs_file_close(&s_lfs, &file);

    return (written == (lfs_ssize_t)sizeof(*meta) && close_err == 0) ? ATT_OK : ATT_ERR_STORAGE;
}

static att_status_t load_ota_meta(att_ota_meta_t *meta)
{
    if (meta == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_OTA_META_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    att_ota_meta_t stored;
    lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, &stored, sizeof(stored));
    (void)lfs_file_close(&s_lfs, &file);
    if (read_len != (lfs_ssize_t)sizeof(stored)) {
        return ATT_ERR_STORAGE;
    }

    uint16_t expected = stored.crc16;
    stored.crc16 = 0u;
    uint16_t actual = ota_meta_crc(&stored);
    stored.crc16 = expected;
    if (stored.magic != ATT_OTA_META_MAGIC ||
        stored.version != ATT_OTA_META_VERSION ||
        stored.size != sizeof(att_ota_meta_t) ||
        expected != actual) {
        return ATT_ERR_CRC;
    }

    *meta = stored;
    return ATT_OK;
}

static uint32_t record_physical_index(const att_record_meta_t *meta, uint32_t logical_index)
{
    return (meta->head + logical_index) % ATT_STORAGE_MAX_RECORDS;
}

static att_status_t validate_record(att_record_t *record)
{
    uint16_t expected = record->crc16;
    record->crc16 = 0u;
    uint16_t actual = record_crc(record);
    record->crc16 = expected;
    return expected == actual ? ATT_OK : ATT_ERR_CRC;
}

void att_storage_default_config(att_device_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->device_id = 1u;
    config->work_mode = ATT_MODE_IN_OUT;
    config->upload_enable = 1u;
    config->repeat_interval_sec = 60u;
    config->timezone = 8;
}

att_status_t att_storage_init(void)
{
    if (s_mounted) {
        return ATT_OK;
    }

    int err = lfs_mount(&s_lfs, &g_att_lfs_cfg);
    if (err != 0) {
        err = lfs_format(&s_lfs, &g_att_lfs_cfg);
        if (err != 0) {
            return ATT_ERR_STORAGE;
        }
        err = lfs_mount(&s_lfs, &g_att_lfs_cfg);
        if (err != 0) {
            return ATT_ERR_STORAGE;
        }
    }

    s_mounted = 1u;

    att_device_config_t config;
    if (att_storage_load_config(&config) != ATT_OK) {
        att_storage_default_config(&config);
        return att_storage_save_config(&config);
    }

    return ATT_OK;
}

att_status_t att_storage_format(void)
{
    if (s_mounted) {
        (void)lfs_unmount(&s_lfs);
        s_mounted = 0u;
    }

    if (lfs_format(&s_lfs, &g_att_lfs_cfg) != 0) {
        return ATT_ERR_STORAGE;
    }
    return att_storage_init();
}

att_status_t att_storage_load_config(att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_CFG_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    att_config_file_t blob;
    lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, &blob, sizeof(blob));
    (void)lfs_file_close(&s_lfs, &file);
    if (read_len != (lfs_ssize_t)sizeof(blob)) {
        return ATT_ERR_STORAGE;
    }

    const uint16_t crc = att_crc16_ccitt_false(&blob, offsetof(att_config_file_t, crc16));
    if (blob.magic != ATT_CFG_MAGIC || blob.version != ATT_CFG_VERSION ||
        blob.size != sizeof(att_device_config_t) || blob.crc16 != crc) {
        return ATT_ERR_CRC;
    }

    *config = blob.config;
    return ATT_OK;
}

att_status_t att_storage_save_config(const att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_config_file_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = ATT_CFG_MAGIC;
    blob.version = ATT_CFG_VERSION;
    blob.size = sizeof(att_device_config_t);
    blob.config = *config;
    blob.crc16 = att_crc16_ccitt_false(&blob, offsetof(att_config_file_t, crc16));

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_CFG_PATH, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return ATT_ERR_STORAGE;
    }
    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, &blob, sizeof(blob));
    int close_err = lfs_file_close(&s_lfs, &file);

    return (written == (lfs_ssize_t)sizeof(blob) && close_err == 0) ? ATT_OK : ATT_ERR_STORAGE;
}

att_status_t att_storage_append_record(const att_record_t *record)
{
    if (record == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_record_t stored = *record;
    stored.crc16 = 0u;
    stored.crc16 = record_crc(&stored);

    att_record_meta_t meta;
    att_status_t status = load_record_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }

    uint32_t physical;
    if (meta.count < ATT_STORAGE_MAX_RECORDS) {
        physical = record_physical_index(&meta, meta.count);
        meta.count++;
    } else {
        physical = meta.head;
        meta.head = (meta.head + 1u) % ATT_STORAGE_MAX_RECORDS;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_PATH, LFS_O_RDWR | LFS_O_CREAT) != 0) {
        return ATT_ERR_STORAGE;
    }
    lfs_soff_t off = (lfs_soff_t)(physical * sizeof(att_record_t));
    if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0) {
        (void)lfs_file_close(&s_lfs, &file);
        return ATT_ERR_STORAGE;
    }
    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, &stored, sizeof(stored));
    int close_err = lfs_file_close(&s_lfs, &file);

    if (written != (lfs_ssize_t)sizeof(stored) || close_err != 0) {
        return ATT_ERR_STORAGE;
    }
    return save_record_meta(&meta);
}

att_status_t att_storage_record_count(uint32_t *count)
{
    if (count == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_record_meta_t meta;
    att_status_t status = load_record_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }

    *count = meta.count;
    return ATT_OK;
}

att_status_t att_storage_read_record(uint32_t index, att_record_t *record)
{
    if (record == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_record_meta_t meta;
    att_status_t status = load_record_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }
    if (index >= meta.count) {
        return ATT_ERR_STORAGE;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    uint32_t physical = record_physical_index(&meta, index);
    lfs_soff_t off = (lfs_soff_t)(physical * sizeof(att_record_t));
    if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0) {
        (void)lfs_file_close(&s_lfs, &file);
        return ATT_ERR_STORAGE;
    }

    lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, record, sizeof(*record));
    (void)lfs_file_close(&s_lfs, &file);
    if (read_len != (lfs_ssize_t)sizeof(*record)) {
        return ATT_ERR_STORAGE;
    }

    return validate_record(record);
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    uint32_t count = 0u;
    att_status_t status = att_storage_record_count(&count);
    if (status != ATT_OK) {
        return status;
    }

    for (uint32_t i = 0; i < count; ++i) {
        status = att_storage_read_record(i, record);
        if (status == ATT_OK && record->upload_state != ATT_UPLOAD_DONE) {
            return ATT_OK;
        }
    }

    return ATT_ERR_NOT_READY;
}

att_status_t att_storage_pending_uploads(att_record_t *records, uint8_t max_records, uint8_t *out_count)
{
    if (records == NULL || out_count == NULL || max_records == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    *out_count = 0u;
    uint32_t count = 0u;
    att_status_t status = att_storage_record_count(&count);
    if (status != ATT_OK) {
        return status;
    }

    for (uint32_t i = 0; i < count && *out_count < max_records; ++i) {
        att_record_t record;
        status = att_storage_read_record(i, &record);
        if (status == ATT_OK && record.upload_state != ATT_UPLOAD_DONE) {
            records[*out_count] = record;
            (*out_count)++;
        }
    }

    return *out_count > 0u ? ATT_OK : ATT_ERR_NOT_READY;
}

att_status_t att_storage_mark_uploaded(uint32_t seq)
{
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_record_meta_t meta;
    att_status_t status = load_record_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_PATH, LFS_O_RDWR) != 0) {
        return ATT_ERR_STORAGE;
    }

    uint8_t found = 0u;
    for (uint32_t i = 0; i < meta.count; ++i) {
        att_record_t record;
        uint32_t physical = record_physical_index(&meta, i);
        lfs_soff_t off = (lfs_soff_t)(physical * sizeof(att_record_t));
        if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0 ||
            lfs_file_read(&s_lfs, &file, &record, sizeof(record)) != (lfs_ssize_t)sizeof(record)) {
            (void)lfs_file_close(&s_lfs, &file);
            return ATT_ERR_STORAGE;
        }
        if (validate_record(&record) != ATT_OK) {
            continue;
        }

        if (record.seq == seq) {
            record.upload_state = ATT_UPLOAD_DONE;
            record.crc16 = 0u;
            record.crc16 = record_crc(&record);
            if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0 ||
                lfs_file_write(&s_lfs, &file, &record, sizeof(record)) != (lfs_ssize_t)sizeof(record)) {
                (void)lfs_file_close(&s_lfs, &file);
                return ATT_ERR_STORAGE;
            }
            found = 1u;
        }
    }

    int close_err = lfs_file_close(&s_lfs, &file);
    if (close_err != 0) {
        return ATT_ERR_STORAGE;
    }
    return found ? ATT_OK : ATT_ERR_NOT_READY;
}

att_status_t att_storage_load_weather(char *text, size_t text_len)
{
    if (text == NULL || text_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    text[0] = '\0';
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_WEATHER_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, text, text_len - 1u);
    (void)lfs_file_close(&s_lfs, &file);
    if (read_len < 0) {
        text[0] = '\0';
        return ATT_ERR_STORAGE;
    }

    text[(size_t)read_len] = '\0';
    return ATT_OK;
}

att_status_t att_storage_save_weather(const char *text)
{
    if (text == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_WEATHER_PATH, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return ATT_ERR_STORAGE;
    }

    size_t len = strlen(text);
    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, text, len);
    int close_err = lfs_file_close(&s_lfs, &file);

    return (written == (lfs_ssize_t)len && close_err == 0) ? ATT_OK : ATT_ERR_STORAGE;
}

att_status_t att_storage_ota_begin(const char *version, uint32_t size_bytes, uint32_t expected_crc32)
{
    if (version == NULL || size_bytes == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    if (size_bytes > ATT_OTA_TARGET_SIZE_BYTES) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    lfs_file_t image;
    if (lfs_file_open(&s_lfs, &image, ATT_OTA_IMAGE_PATH,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) {
        return ATT_ERR_STORAGE;
    }
    if (lfs_file_close(&s_lfs, &image) != 0) {
        return ATT_ERR_STORAGE;
    }

    att_ota_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic = ATT_OTA_META_MAGIC;
    meta.version = ATT_OTA_META_VERSION;
    meta.size = sizeof(att_ota_meta_t);
    snprintf(meta.image_version, sizeof(meta.image_version), "%s", version);
    meta.size_bytes = size_bytes;
    meta.expected_crc32 = expected_crc32;
    meta.target_addr = ATT_APP_SLOT_BASE_ADDR;
    meta.target_slot = ATT_OTA_TARGET_SLOT_ID;
    meta.install_state = ATT_OTA_INSTALL_NONE;
    meta.install_error = ATT_OTA_INSTALL_ERR_NONE;
    return save_ota_meta(&meta);
}

att_status_t att_storage_ota_write(uint32_t offset, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_ota_meta_t meta;
    att_status_t status = load_ota_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }
    if (offset > meta.size_bytes || len > (size_t)(meta.size_bytes - offset)) {
        return ATT_ERR_INVALID_ARG;
    }

    lfs_file_t image;
    if (lfs_file_open(&s_lfs, &image, ATT_OTA_IMAGE_PATH, LFS_O_RDWR | LFS_O_CREAT) != 0) {
        return ATT_ERR_STORAGE;
    }
    if (lfs_file_seek(&s_lfs, &image, (lfs_soff_t)offset, LFS_SEEK_SET) < 0) {
        (void)lfs_file_close(&s_lfs, &image);
        return ATT_ERR_STORAGE;
    }
    lfs_ssize_t written = lfs_file_write(&s_lfs, &image, data, len);
    int close_err = lfs_file_close(&s_lfs, &image);
    if (written != (lfs_ssize_t)len || close_err != 0) {
        return ATT_ERR_STORAGE;
    }

    uint32_t end = offset + (uint32_t)len;
    if (end > meta.received_bytes) {
        meta.received_bytes = end;
    }
    meta.verified = 0u;
    meta.actual_crc32 = 0u;
    meta.install_state = ATT_OTA_INSTALL_NONE;
    meta.install_error = ATT_OTA_INSTALL_ERR_NONE;
    return save_ota_meta(&meta);
}

att_status_t att_storage_ota_mark_verified(uint32_t actual_crc32)
{
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_ota_meta_t meta;
    att_status_t status = load_ota_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }
    meta.actual_crc32 = actual_crc32;
    meta.verified = (actual_crc32 == meta.expected_crc32 &&
                     meta.received_bytes == meta.size_bytes) ? 1u : 0u;
    meta.install_state = meta.verified ? ATT_OTA_INSTALL_PENDING : ATT_OTA_INSTALL_FAILED;
    meta.install_error = meta.verified ? ATT_OTA_INSTALL_ERR_NONE : ATT_OTA_INSTALL_ERR_IMAGE_CRC;
    return save_ota_meta(&meta);
}

att_status_t att_storage_ota_status(att_ota_file_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_ota_meta_t meta;
    att_status_t load_status = load_ota_meta(&meta);
    if (load_status != ATT_OK) {
        return load_status;
    }

    status->valid = 1u;
    status->verified = meta.verified;
    status->current_slot = ATT_APP_SLOT_ID;
    status->target_slot = meta.target_slot;
    snprintf(status->version, sizeof(status->version), "%s", meta.image_version);
    status->size_bytes = meta.size_bytes;
    status->received_bytes = meta.received_bytes;
    status->expected_crc32 = meta.expected_crc32;
    status->actual_crc32 = meta.actual_crc32;
    status->target_addr = meta.target_addr;
    status->install_state = meta.install_state;
    status->install_error = meta.install_error;
    return ATT_OK;
}

att_status_t att_storage_ota_verify(uint32_t *actual_crc32)
{
    if (actual_crc32 == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_ota_meta_t meta;
    att_status_t status = load_ota_meta(&meta);
    if (status != ATT_OK) {
        return status;
    }
    if (meta.received_bytes != meta.size_bytes) {
        return ATT_ERR_NOT_READY;
    }

    lfs_file_t image;
    if (lfs_file_open(&s_lfs, &image, ATT_OTA_IMAGE_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    uint8_t buffer[64];
    uint32_t remaining = meta.size_bytes;
    uint32_t crc = att_crc32_init();
    while (remaining > 0u) {
        size_t want = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        lfs_ssize_t got = lfs_file_read(&s_lfs, &image, buffer, want);
        if (got != (lfs_ssize_t)want) {
            (void)lfs_file_close(&s_lfs, &image);
            return ATT_ERR_STORAGE;
        }
        crc = att_crc32_update(crc, buffer, want);
        remaining -= (uint32_t)want;
    }

    if (lfs_file_close(&s_lfs, &image) != 0) {
        return ATT_ERR_STORAGE;
    }

    *actual_crc32 = att_crc32_finish(crc);
    return att_storage_ota_mark_verified(*actual_crc32);
}

att_status_t att_storage_boot_state_load(att_boot_state_t *state)
{
    if (state == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_status_t status = load_boot_state(state);
    if (status == ATT_OK) {
        return ATT_OK;
    }

    boot_state_init(state);
    return save_boot_state(state);
}

att_status_t att_storage_boot_state_save(att_boot_state_t *state)
{
    if (state == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }
    return save_boot_state(state);
}

att_status_t att_storage_boot_confirm_current(void)
{
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    att_boot_state_t state;
    att_status_t status = load_boot_state(&state);
    if (status != ATT_OK) {
        boot_state_init(&state);
    }

    if (ATT_APP_SLOT_ID != 0u) {
        state.active_slot = ATT_APP_SLOT_ID;
        state.confirmed_slot = ATT_APP_SLOT_ID;
        if (state.pending_slot == ATT_APP_SLOT_ID) {
            state.pending_slot = 0u;
        }
    }
    return save_boot_state(&state);
}
