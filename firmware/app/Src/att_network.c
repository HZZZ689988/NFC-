#include "att_network.h"

#include <stdio.h>
#include <string.h>

#include "att_storage.h"
#include "esp01s.h"

static att_device_config_t s_config;
static uint8_t s_configured;

att_status_t att_network_init(const att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    s_config = *config;
    s_configured = 1u;

    ESP01S_Config_t esp_cfg;
    memset(&esp_cfg, 0, sizeof(esp_cfg));
    snprintf(esp_cfg.ssid, sizeof(esp_cfg.ssid), "%s", s_config.wifi_ssid);
    snprintf(esp_cfg.password, sizeof(esp_cfg.password), "%s", s_config.wifi_password);
    snprintf(esp_cfg.tcpServerIP, sizeof(esp_cfg.tcpServerIP), "%s", s_config.server_host);
    esp_cfg.tcpPort = s_config.server_port;
    snprintf(esp_cfg.ntpServer, sizeof(esp_cfg.ntpServer), "%s", "ntp.aliyun.com");
    esp_cfg.ntpTimezone = s_config.timezone;
    ESP01S_SetConfig(&esp_cfg);

    return ATT_OK;
}

att_status_t att_network_sync_time(void)
{
    if (!s_configured) {
        return ATT_ERR_NOT_READY;
    }
    ESP01S_SyncNtpTime();
    return ESP01S_IsNtpSynced() ? ATT_OK : ATT_ERR;
}

att_status_t att_network_query_weather(void)
{
    if (!s_configured || s_config.weather_key[0] == '\0' || s_config.weather_location[0] == '\0') {
        return ATT_ERR_NOT_READY;
    }

    char city[16];
    char day[32];
    char high[8];
    char night[32];
    char low[8];
    char precip[8];
    int ret = ESP01S_QueryWeather(s_config.weather_key, s_config.weather_location,
                                  "zh-Hans", "c", city, sizeof(city),
                                  day, sizeof(day), high, sizeof(high),
                                  night, sizeof(night), low, sizeof(low),
                                  precip, sizeof(precip));
    return ret == 0 ? ATT_OK : ATT_ERR;
}

att_status_t att_network_upload_pending(void)
{
    if (!s_configured || !s_config.upload_enable) {
        return ATT_ERR_NOT_READY;
    }

    att_record_t record;
    att_status_t status = att_storage_next_pending_upload(&record);
    if (status != ATT_OK) {
        return status;
    }

    char frame[160];
    snprintf(frame, sizeof(frame),
             "UPLOAD:SEQ=%lu|UID=%02X%02X%02X%02X|SID=%lu|TYPE=%u|TS=%lu|DEV=%lu\n",
             (unsigned long)record.seq,
             record.uid.bytes[0], record.uid.bytes[1], record.uid.bytes[2], record.uid.bytes[3],
             (unsigned long)record.sid,
             (unsigned int)record.type,
             (unsigned long)record.timestamp,
             (unsigned long)record.device_id);
    ESP01S_SendStr(frame);

    return ATT_OK;
}

att_status_t att_network_send_heartbeat(void)
{
    if (!s_configured || !s_config.upload_enable) {
        return ATT_ERR_NOT_READY;
    }

    char frame[80];
    snprintf(frame, sizeof(frame), "HEARTBEAT:DEV=%lu\n", (unsigned long)s_config.device_id);
    ESP01S_SendStr(frame);
    return ATT_OK;
}
