#include "att_storage.h"

#include <stddef.h>
#include <string.h>

#include "att_crc16.h"
#include "lfs.h"

#define ATT_CFG_MAGIC       0x41544346u
#define ATT_CFG_VERSION     1u
#define ATT_CFG_PATH        "config.bin"
#define ATT_RECORD_PATH     "records.bin"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    att_device_config_t config;
    uint16_t crc16;
} att_config_file_t;

extern const struct lfs_config g_att_lfs_cfg;

static lfs_t s_lfs;
static uint8_t s_mounted;

static att_status_t ensure_mounted(void)
{
    return s_mounted ? ATT_OK : ATT_ERR_NOT_READY;
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
    stored.crc16 = att_crc16_ccitt_false(&stored, offsetof(att_record_t, crc16));

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_PATH, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND) != 0) {
        return ATT_ERR_STORAGE;
    }
    lfs_ssize_t written = lfs_file_write(&s_lfs, &file, &stored, sizeof(stored));
    int close_err = lfs_file_close(&s_lfs, &file);

    return (written == (lfs_ssize_t)sizeof(stored) && close_err == 0) ? ATT_OK : ATT_ERR_STORAGE;
}

att_status_t att_storage_record_count(uint32_t *count)
{
    if (count == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    struct lfs_info info;
    if (lfs_stat(&s_lfs, ATT_RECORD_PATH, &info) != 0) {
        *count = 0u;
        return ATT_OK;
    }

    *count = (uint32_t)(info.size / sizeof(att_record_t));
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

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_PATH, LFS_O_RDONLY) != 0) {
        return ATT_ERR_STORAGE;
    }

    lfs_soff_t off = (lfs_soff_t)(index * sizeof(att_record_t));
    if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0) {
        (void)lfs_file_close(&s_lfs, &file);
        return ATT_ERR_STORAGE;
    }

    lfs_ssize_t read_len = lfs_file_read(&s_lfs, &file, record, sizeof(*record));
    (void)lfs_file_close(&s_lfs, &file);
    if (read_len != (lfs_ssize_t)sizeof(*record)) {
        return ATT_ERR_STORAGE;
    }

    uint16_t expected = record->crc16;
    record->crc16 = 0u;
    uint16_t actual = att_crc16_ccitt_false(record, offsetof(att_record_t, crc16));
    record->crc16 = expected;

    return expected == actual ? ATT_OK : ATT_ERR_CRC;
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

att_status_t att_storage_mark_uploaded(uint32_t seq)
{
    if (ensure_mounted() != ATT_OK) {
        return ATT_ERR_NOT_READY;
    }

    uint32_t count = 0u;
    if (att_storage_record_count(&count) != ATT_OK) {
        return ATT_ERR_STORAGE;
    }

    lfs_file_t file;
    if (lfs_file_open(&s_lfs, &file, ATT_RECORD_PATH, LFS_O_RDWR) != 0) {
        return ATT_ERR_STORAGE;
    }

    for (uint32_t i = 0; i < count; ++i) {
        att_record_t record;
        lfs_soff_t off = (lfs_soff_t)(i * sizeof(att_record_t));
        if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0 ||
            lfs_file_read(&s_lfs, &file, &record, sizeof(record)) != (lfs_ssize_t)sizeof(record)) {
            (void)lfs_file_close(&s_lfs, &file);
            return ATT_ERR_STORAGE;
        }

        if (record.seq == seq) {
            record.upload_state = ATT_UPLOAD_DONE;
            record.crc16 = 0u;
            record.crc16 = att_crc16_ccitt_false(&record, offsetof(att_record_t, crc16));
            if (lfs_file_seek(&s_lfs, &file, off, LFS_SEEK_SET) < 0 ||
                lfs_file_write(&s_lfs, &file, &record, sizeof(record)) != (lfs_ssize_t)sizeof(record)) {
                (void)lfs_file_close(&s_lfs, &file);
                return ATT_ERR_STORAGE;
            }
            int close_err = lfs_file_close(&s_lfs, &file);
            return close_err == 0 ? ATT_OK : ATT_ERR_STORAGE;
        }
    }

    (void)lfs_file_close(&s_lfs, &file);
    return ATT_ERR_NOT_READY;
}
