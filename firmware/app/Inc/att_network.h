#ifndef ATT_NETWORK_H
#define ATT_NETWORK_H

#include "attendance_types.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t configured;
    uint8_t upload_enabled;
    uint8_t esp_state;
    uint8_t ntp_synced;
    uint8_t blacklist_count;
    uint32_t device_id;
    uint16_t server_port;
    char ssid[ATT_WIFI_SSID_MAX_LEN];
    char server_host[ATT_SERVER_HOST_MAX_LEN];
} att_network_status_t;

typedef enum {
    ATT_OTA_STATE_IDLE = 0,
    ATT_OTA_STATE_NONE,
    ATT_OTA_STATE_AVAILABLE,
    ATT_OTA_STATE_DOWNLOADING,
    ATT_OTA_STATE_READY,
    ATT_OTA_STATE_ERROR,
} att_ota_state_t;

typedef struct {
    att_ota_state_t state;
    char version[16];
    uint32_t target_slot;
    uint32_t size_bytes;
    uint32_t received_bytes;
    uint32_t expected_crc32;
    uint32_t actual_crc32;
    uint16_t chunk_size;
} att_ota_status_t;

att_status_t att_network_init(const att_device_config_t *config);
att_status_t att_network_sync_time(void);
att_status_t att_network_query_weather(char *text, size_t text_len);
att_status_t att_network_upload_pending(void);
att_status_t att_network_upload_pending_batch(uint8_t max_records);
att_status_t att_network_send_heartbeat(void);
att_status_t att_network_query_blacklist(void);
uint8_t att_network_uid_is_blacklisted(const att_uid_t *uid);
att_status_t att_network_get_status(att_network_status_t *status);
att_status_t att_network_query_ota(void);
att_status_t att_network_get_ota_status(att_ota_status_t *status);
void att_network_handle_rx(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
