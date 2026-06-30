#ifndef ATT_STORAGE_H
#define ATT_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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
att_status_t att_storage_load_weather(char *text, size_t text_len);
att_status_t att_storage_save_weather(const char *text);

#ifdef __cplusplus
}
#endif

#endif
