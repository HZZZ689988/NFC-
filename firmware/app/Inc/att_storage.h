#ifndef ATT_STORAGE_H
#define ATT_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "att_ota_format.h"
#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t valid;
    uint8_t verified;
    uint32_t current_slot;
    uint32_t target_slot;
    char version[16];
    uint32_t size_bytes;
    uint32_t received_bytes;
    uint32_t expected_crc32;
    uint32_t actual_crc32;
    uint32_t target_addr;
    uint32_t install_state;
    uint32_t install_error;
} att_ota_file_status_t;

att_status_t att_storage_init(void);
att_status_t att_storage_format(void);

att_status_t att_storage_load_config(att_device_config_t *config);
att_status_t att_storage_save_config(const att_device_config_t *config);
void att_storage_default_config(att_device_config_t *config);

att_status_t att_storage_append_record(const att_record_t *record);
att_status_t att_storage_read_record(uint32_t index, att_record_t *record);
att_status_t att_storage_record_count(uint32_t *count);
att_status_t att_storage_mark_uploaded(uint32_t seq);

att_status_t att_storage_next_pending_upload(att_record_t *record);
att_status_t att_storage_pending_uploads(att_record_t *records, uint8_t max_records, uint8_t *count);
att_status_t att_storage_load_weather(char *text, size_t text_len);
att_status_t att_storage_save_weather(const char *text);
att_status_t att_storage_ota_begin(const char *version, uint32_t size_bytes, uint32_t expected_crc32);
att_status_t att_storage_ota_write(uint32_t offset, const uint8_t *data, size_t len);
att_status_t att_storage_ota_mark_verified(uint32_t actual_crc32);
att_status_t att_storage_ota_status(att_ota_file_status_t *status);
att_status_t att_storage_ota_verify(uint32_t *actual_crc32);
att_status_t att_storage_boot_state_load(att_boot_state_t *state);
att_status_t att_storage_boot_state_save(att_boot_state_t *state);
att_status_t att_storage_boot_confirm_current(void);

#ifdef __cplusplus
}
#endif

#endif
