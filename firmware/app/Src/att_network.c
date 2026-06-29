#include "att_network.h"

#include <stdio.h>
#include <string.h>

#include "att_storage.h"
#include "esp01s.h"

#define ATT_NETWORK_RX_LINE_MAX 63u

static att_device_config_t s_config;
static uint8_t s_configured;
static char s_rx_line[ATT_NETWORK_RX_LINE_MAX + 1u];
static size_t s_rx_line_len;
static uint8_t s_rx_line_overflow;

static void network_rx_callback(const uint8_t *data, uint16_t len, void *ctx);

static att_status_t copy_config_string(char *dest, size_t dest_len, const char *src)
{
    if (dest == NULL || src == NULL || dest_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    size_t i = 0u;
    while (i < dest_len - 1u && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    if (src[i] != '\0') {
        dest[0] = '\0';
        return ATT_ERR_INVALID_ARG;
    }

    dest[i] = '\0';
    return ATT_OK;
}

static uint8_t parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return 0u;
    }

    uint32_t parsed = 0u;
    const char *p = text;
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return 0u;
        }

        uint32_t digit = (uint32_t)(*p - '0');
        if (parsed > (UINT32_MAX - digit) / 10u) {
            return 0u;
        }
        parsed = (parsed * 10u) + digit;
        p++;
    }

    *value = parsed;
    return 1u;
}

static void handle_rx_line(const char *line)
{
    static const char upload_ack_prefix[] = "ACK:UPLOAD:";

    if (line == NULL) {
        return;
    }

    if (strncmp(line, upload_ack_prefix, sizeof(upload_ack_prefix) - 1u) == 0) {
        uint32_t seq = 0u;
        if (parse_u32(line + sizeof(upload_ack_prefix) - 1u, &seq) != 0u) {
            (void)att_storage_mark_uploaded(seq);
        }
    }
}

static void network_rx_callback(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    att_network_handle_rx(data, (size_t)len);
}

att_status_t att_network_init(const att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    ESP01S_Config_t esp_cfg;
    memset(&esp_cfg, 0, sizeof(esp_cfg));
    if (copy_config_string(esp_cfg.ssid, sizeof(esp_cfg.ssid), config->wifi_ssid) != ATT_OK ||
        copy_config_string(esp_cfg.password, sizeof(esp_cfg.password), config->wifi_password) != ATT_OK ||
        copy_config_string(esp_cfg.tcpServerIP, sizeof(esp_cfg.tcpServerIP), config->server_host) != ATT_OK ||
        copy_config_string(esp_cfg.ntpServer, sizeof(esp_cfg.ntpServer), "ntp.aliyun.com") != ATT_OK) {
        return ATT_ERR_INVALID_ARG;
    }
    esp_cfg.tcpPort = config->server_port;
    esp_cfg.ntpTimezone = config->timezone;
    ESP01S_SetConfig(&esp_cfg);
    ESP01S_RegisterDataCb(network_rx_callback, NULL);

    s_config = *config;
    s_configured = 1u;
    s_rx_line_len = 0u;
    s_rx_line_overflow = 0u;

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

void att_network_handle_rx(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return;
    }

    for (size_t i = 0u; i < len; ++i) {
        uint8_t ch = data[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (s_rx_line_overflow == 0u && s_rx_line_len > 0u) {
                s_rx_line[s_rx_line_len] = '\0';
                handle_rx_line(s_rx_line);
            }
            s_rx_line_len = 0u;
            s_rx_line_overflow = 0u;
            continue;
        }

        if (s_rx_line_overflow != 0u) {
            continue;
        }

        if (s_rx_line_len >= ATT_NETWORK_RX_LINE_MAX) {
            s_rx_line_len = 0u;
            s_rx_line_overflow = 1u;
            continue;
        }

        s_rx_line[s_rx_line_len++] = (char)ch;
    }
}
