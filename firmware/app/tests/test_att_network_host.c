#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_network.h"
#include "esp01s.h"

static ESP01S_Config_t g_last_config;
static unsigned g_set_config_calls;
static char g_last_sent[256];

static void require_int(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static void reset_mocks(void)
{
    memset(&g_last_config, 0, sizeof(g_last_config));
    memset(g_last_sent, 0, sizeof(g_last_sent));
    g_set_config_calls = 0u;
}

static att_device_config_t base_config(void)
{
    att_device_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.wifi_ssid, sizeof(config.wifi_ssid), "%s", "test-ssid");
    snprintf(config.wifi_password, sizeof(config.wifi_password), "%s", "test-password");
    snprintf(config.server_host, sizeof(config.server_host), "%s", "192.168.1.10");
    config.server_port = 9000u;
    config.upload_enable = 1u;
    config.timezone = 8;
    config.device_id = 42u;
    return config;
}

static void test_network_init_rejects_host_that_does_not_fit_esp_config(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    memset(config.server_host, 'a', sizeof(config.server_host) - 1u);
    config.server_host[sizeof(config.server_host) - 1u] = '\0';

    att_status_t status = att_network_init(&config);

    require_int(status == ATT_ERR_INVALID_ARG, "overlong server host should be rejected");
    require_int(g_set_config_calls == 0u, "overlong server host should not update ESP config");
}

static void test_network_init_copies_valid_config_to_esp_driver(void)
{
    reset_mocks();
    att_device_config_t config = base_config();

    att_status_t status = att_network_init(&config);

    require_int(status == ATT_OK, "valid network config should initialize");
    require_int(g_set_config_calls == 1u, "valid network config should update ESP config");
    require_int(strcmp(g_last_config.ssid, "test-ssid") == 0, "ssid should be copied");
    require_int(strcmp(g_last_config.password, "test-password") == 0, "password should be copied");
    require_int(strcmp(g_last_config.tcpServerIP, "192.168.1.10") == 0, "server host should be copied");
    require_int(g_last_config.tcpPort == 9000u, "server port should be copied");
    require_int(strcmp(g_last_config.ntpServer, "ntp.aliyun.com") == 0, "ntp server should be configured");
    require_int(g_last_config.ntpTimezone == 8, "timezone should be copied");
}

int main(void)
{
    test_network_init_rejects_host_that_does_not_fit_esp_config();
    test_network_init_copies_valid_config_to_esp_driver();
    return 0;
}

void ESP01S_SetConfig(const ESP01S_Config_t *pConfig)
{
    if (pConfig != NULL) {
        g_last_config = *pConfig;
        g_set_config_calls++;
    }
}

void ESP01S_SendStr(const char *str)
{
    if (str != NULL) {
        strncat(g_last_sent, str, sizeof(g_last_sent) - strlen(g_last_sent) - 1u);
    }
}

void ESP01S_SyncNtpTime(void)
{
}

uint8_t ESP01S_IsNtpSynced(void)
{
    return 1u;
}

int ESP01S_QueryWeather(const char *apiKey, const char *location,
                        const char *language, const char *unit,
                        char *outCity, uint16_t cityBufSize,
                        char *outTextDay, uint16_t textDayBufSize,
                        char *outHigh, uint16_t highBufSize,
                        char *outTextNight, uint16_t textNightBufSize,
                        char *outLow, uint16_t lowBufSize,
                        char *outPrecip, uint16_t precipBufSize)
{
    (void)apiKey;
    (void)location;
    (void)language;
    (void)unit;
    (void)outCity;
    (void)cityBufSize;
    (void)outTextDay;
    (void)textDayBufSize;
    (void)outHigh;
    (void)highBufSize;
    (void)outTextNight;
    (void)textNightBufSize;
    (void)outLow;
    (void)lowBufSize;
    (void)outPrecip;
    (void)precipBufSize;
    return 0;
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_NOT_READY;
}
