#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_crc32.h"
#include "att_network.h"
#include "att_storage.h"
#include "esp01s.h"

static ESP01S_Config_t g_last_config;
static unsigned g_set_config_calls;
static ESP01S_DataCallback_t g_data_cb;
static void *g_data_cb_ctx;
static char g_last_sent[512];
static uint32_t g_last_mark_uploaded_seq;
static uint32_t g_mark_uploaded_seqs[8];
static unsigned g_mark_uploaded_calls;
static unsigned g_weather_calls;
static att_record_t g_pending_records[4];
static uint8_t g_pending_count;
static att_status_t g_pending_status;
static int16_t g_temperature_centi_c;
static att_status_t g_temperature_status;
static ESP01S_State_t g_esp_state;
static uint8_t g_ntp_synced;
static att_ota_file_status_t g_cached_ota;
static att_status_t g_ota_status_status;
static att_status_t g_ota_begin_status;
static att_status_t g_ota_write_status;
static att_status_t g_ota_verify_status;
static unsigned g_ota_begin_calls;
static unsigned g_ota_write_calls;
static unsigned g_ota_verify_calls;
static char g_ota_begin_version[16];
static uint32_t g_ota_begin_size;
static uint32_t g_ota_begin_crc32;
static uint8_t g_ota_image[256];
static uint32_t g_ota_actual_crc32;

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
    g_data_cb = NULL;
    g_data_cb_ctx = NULL;
    g_last_mark_uploaded_seq = 0u;
    memset(g_mark_uploaded_seqs, 0, sizeof(g_mark_uploaded_seqs));
    g_mark_uploaded_calls = 0u;
    g_weather_calls = 0u;
    memset(g_pending_records, 0, sizeof(g_pending_records));
    g_pending_count = 0u;
    g_pending_status = ATT_ERR_NOT_READY;
    g_temperature_centi_c = 2650;
    g_temperature_status = ATT_OK;
    g_esp_state = ESP01S_STATE_TRANSPARENT;
    g_ntp_synced = 1u;
    memset(&g_cached_ota, 0, sizeof(g_cached_ota));
    g_ota_status_status = ATT_ERR_STORAGE;
    g_ota_begin_status = ATT_OK;
    g_ota_write_status = ATT_OK;
    g_ota_verify_status = ATT_OK;
    g_ota_begin_calls = 0u;
    g_ota_write_calls = 0u;
    g_ota_verify_calls = 0u;
    memset(g_ota_begin_version, 0, sizeof(g_ota_begin_version));
    g_ota_begin_size = 0u;
    g_ota_begin_crc32 = 0u;
    memset(g_ota_image, 0, sizeof(g_ota_image));
    g_ota_actual_crc32 = 0u;
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
    require_int(g_data_cb != NULL, "network init should register ESP transparent data callback");
}

static void test_upload_ack_marks_matching_record_uploaded(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before ACK handling");
    require_int(g_data_cb != NULL, "ACK test needs registered ESP data callback");

    const char ack[] = "ACK:UPLOAD:42\n";
    g_data_cb((const uint8_t *)ack, (uint16_t)strlen(ack), g_data_cb_ctx);

    require_int(g_mark_uploaded_calls == 1u, "upload ACK should mark one record uploaded");
    require_int(g_last_mark_uploaded_seq == 42u, "upload ACK should mark the ACK sequence");
}

static void test_upload_ack_ignores_invalid_sequence(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before invalid ACK handling");
    require_int(g_data_cb != NULL, "invalid ACK test needs registered ESP data callback");

    const char ack[] = "ACK:UPLOAD:not-a-number\n";
    g_data_cb((const uint8_t *)ack, (uint16_t)strlen(ack), g_data_cb_ctx);

    require_int(g_mark_uploaded_calls == 0u, "invalid upload ACK should not mark records uploaded");
}

static void test_upload_ack_handles_split_lines(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before split ACK handling");
    require_int(g_data_cb != NULL, "split ACK test needs registered ESP data callback");

    const char part1[] = "ACK:UP";
    const char part2[] = "LOAD:77\n";
    g_data_cb((const uint8_t *)part1, (uint16_t)strlen(part1), g_data_cb_ctx);
    g_data_cb((const uint8_t *)part2, (uint16_t)strlen(part2), g_data_cb_ctx);

    require_int(g_mark_uploaded_calls == 1u, "split upload ACK should mark one record uploaded");
    require_int(g_last_mark_uploaded_seq == 77u, "split upload ACK should mark the ACK sequence");
}

static void test_upload_ack_handles_prefixed_ipd_line(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before +IPD ACK handling");
    require_int(g_data_cb != NULL, "+IPD ACK test needs registered ESP data callback");

    const char ack[] = "+IPD,14:ACK:UPLOAD:88\n";
    g_data_cb((const uint8_t *)ack, (uint16_t)strlen(ack), g_data_cb_ctx);

    require_int(g_mark_uploaded_calls == 1u, "prefixed upload ACK should mark one record uploaded");
    require_int(g_last_mark_uploaded_seq == 88u, "prefixed upload ACK should mark the ACK sequence");
}

static void make_pending_record(uint8_t index, uint32_t seq)
{
    att_record_t *record = &g_pending_records[index];
    memset(record, 0, sizeof(*record));
    record->seq = seq;
    record->uid.bytes[0] = (uint8_t)(0xA0u + index);
    record->uid.bytes[1] = 0xB1u;
    record->uid.bytes[2] = 0xC2u;
    record->uid.bytes[3] = 0xD3u;
    record->sid = 1000u + seq;
    record->type = (index & 1u) ? ATT_RECORD_OUT : ATT_RECORD_IN;
    record->timestamp = 1782691200u + seq;
    record->device_id = 42u;
    record->upload_state = ATT_UPLOAD_PENDING;
}

static void test_batch_upload_sends_multiple_records_and_ack_marks_each(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before batch upload");

    make_pending_record(0u, 50u);
    make_pending_record(1u, 51u);
    make_pending_record(2u, 52u);
    g_pending_count = 3u;
    g_pending_status = ATT_OK;

    require_int(att_network_upload_pending_batch(3u) == ATT_OK, "batch upload should send frame");
    require_int(strstr(g_last_sent, "UPLOADB:DEV=42|R=50,A0B1C2D3,1050,0,1782691250;") != NULL,
                "batch upload should include first record");
    require_int(strstr(g_last_sent, "51,A1B1C2D3,1051,1,1782691251;") != NULL,
                "batch upload should include second record");
    require_int(strstr(g_last_sent, "52,A2B1C2D3,1052,0,1782691252") != NULL,
                "batch upload should include third record");

    const char ack[] = "ACK:UPLOADB:50,51,52\n";
    g_data_cb((const uint8_t *)ack, (uint16_t)strlen(ack), g_data_cb_ctx);

    require_int(g_mark_uploaded_calls == 3u, "batch ACK should mark each uploaded seq");
    require_int(g_mark_uploaded_seqs[0] == 50u, "batch ACK should mark seq 50");
    require_int(g_mark_uploaded_seqs[1] == 51u, "batch ACK should mark seq 51");
    require_int(g_mark_uploaded_seqs[2] == 52u, "batch ACK should mark seq 52");
}

static void test_blacklist_response_updates_lookup(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before blacklist sync");
    require_int(att_network_query_blacklist() == ATT_OK, "blacklist query should send request");
    require_int(strstr(g_last_sent, "CRC:BL?*") != NULL,
                "blacklist query should use CRC-wrapped BL? frame");

    const char line[] = "BL:COUNT=2|UIDS=AABBCCDD,11223344\n";
    g_data_cb((const uint8_t *)line, (uint16_t)strlen(line), g_data_cb_ctx);

    att_uid_t blocked = {{0xAAu, 0xBBu, 0xCCu, 0xDDu}};
    att_uid_t other = {{0x12u, 0x34u, 0x56u, 0x78u}};
    require_int(att_network_uid_is_blacklisted(&blocked) == 1u,
                "synced blacklist UID should be rejected locally");
    require_int(att_network_uid_is_blacklisted(&other) == 0u,
                "UID not in blacklist should remain allowed");
}

static void test_heartbeat_sends_live_temperature(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    g_temperature_centi_c = 3140;
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before heartbeat");

    require_int(att_network_send_heartbeat() == ATT_OK, "heartbeat should send frame");
    require_int(strstr(g_last_sent, "HEARTBEAT:DEV=42|TEMP=31.4") != NULL,
                "heartbeat should include measured temperature");
    require_int(strstr(g_last_sent, "CRC:") != NULL,
                "heartbeat should be CRC wrapped");
}

static void test_network_status_reports_runtime_state(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    snprintf(config.wifi_ssid, sizeof(config.wifi_ssid), "%s", "TEST-5G-WPA3");
    snprintf(config.server_host, sizeof(config.server_host), "%s", "192.168.5.94");
    config.server_port = 9000u;
    g_esp_state = ESP01S_STATE_WIFI_CONNECTING;
    g_ntp_synced = 0u;
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before status");

    att_network_status_t status;
    require_int(att_network_get_status(&status) == ATT_OK, "network status should be readable");
    require_int(status.configured == 1u, "network status should mark configured");
    require_int(status.upload_enabled == 1u, "network status should include upload enable");
    require_int(status.esp_state == (uint8_t)ESP01S_STATE_WIFI_CONNECTING,
                "network status should include ESP state");
    require_int(status.ntp_synced == 0u, "network status should include NTP state");
    require_int(status.device_id == 42u, "network status should include device id");
    require_int(status.server_port == 9000u, "network status should include server port");
    require_int(strcmp(status.ssid, "TEST-5G-WPA3") == 0, "network status should include SSID");
    require_int(strcmp(status.server_host, "192.168.5.94") == 0,
                "network status should include server host");
}

static void test_crc_wrapped_ack_is_verified_before_marking_upload(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before CRC ACK handling");

    const char valid[] = "CRC:ACK:UPLOAD:91*5CCF\n";
    g_data_cb((const uint8_t *)valid, (uint16_t)strlen(valid), g_data_cb_ctx);
    require_int(g_mark_uploaded_calls == 1u, "valid CRC ACK should mark uploaded record");
    require_int(g_last_mark_uploaded_seq == 91u, "valid CRC ACK should mark expected sequence");

    const char invalid[] = "CRC:ACK:UPLOAD:92*0000\n";
    g_data_cb((const uint8_t *)invalid, (uint16_t)strlen(invalid), g_data_cb_ctx);
    require_int(g_mark_uploaded_calls == 1u, "invalid CRC ACK should be ignored");
}

static void test_weather_query_returns_compact_oled_text(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    snprintf(config.weather_key, sizeof(config.weather_key), "%s", "test-key");
    snprintf(config.weather_location, sizeof(config.weather_location), "%s", "hangzhou");
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before weather query");

    char text[32];
    att_status_t status = att_network_query_weather(text, sizeof(text));

    require_int(status == ATT_OK, "weather query should return ATT_OK");
    require_int(g_weather_calls == 1u, "weather query should call ESP driver");
    require_int(strcmp(text, "Sunny 20C") == 0,
                "weather query should return compact OLED text");
}

static void test_ota_query_downloads_chunks_and_verifies_image(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before OTA query");

    const uint8_t image[] = {0x01u, 0x02u, 0xA5u};
    uint32_t image_crc = att_crc32_ieee(image, sizeof(image));
    uint32_t chunk0_crc = att_crc32_ieee(image, 2u);
    uint32_t chunk1_crc = att_crc32_ieee(image + 2u, 1u);
    g_ota_actual_crc32 = image_crc;

    require_int(att_network_query_ota() == ATT_OK, "OTA query should send request");
    require_int(strstr(g_last_sent, "CRC:OTA?SLOT=") != NULL,
                "OTA query should be CRC wrapped with target slot");

    char response[128];
    snprintf(response, sizeof(response), "OTA:VERSION=v1|SIZE=3|CRC32=%08lX|CHUNK=2|SLOT=%c\n",
             (unsigned long)image_crc,
             ATT_OTA_TARGET_SLOT_ID == 2u ? 'B' : 'A');
    g_data_cb((const uint8_t *)response, (uint16_t)strlen(response), g_data_cb_ctx);

    require_int(g_ota_begin_calls == 1u, "OTA advert should create local image file");
    require_int(strcmp(g_ota_begin_version, "v1") == 0, "OTA version should be stored");
    require_int(g_ota_begin_size == 3u, "OTA size should be stored");
    require_int(g_ota_begin_crc32 == image_crc, "OTA expected CRC should be stored");
    require_int(strstr(g_last_sent, "OTA:GET:OFFSET=0|LEN=2|SLOT=") != NULL,
                "first OTA chunk should be requested for target slot");

    snprintf(response, sizeof(response), "OTA:DATA:OFFSET=0|LEN=2|CRC32=%08lX|HEX=0102\n",
             (unsigned long)chunk0_crc);
    g_data_cb((const uint8_t *)response, (uint16_t)strlen(response), g_data_cb_ctx);
    require_int(g_ota_write_calls == 1u, "first OTA chunk should be written");
    require_int(strstr(g_last_sent, "OTA:GET:OFFSET=2|LEN=1|SLOT=") != NULL,
                "second OTA chunk should be requested for target slot");

    snprintf(response, sizeof(response), "OTA:DATA:OFFSET=2|LEN=1|CRC32=%08lX|HEX=A5\n",
             (unsigned long)chunk1_crc);
    g_data_cb((const uint8_t *)response, (uint16_t)strlen(response), g_data_cb_ctx);

    att_ota_status_t status;
    require_int(att_network_get_ota_status(&status) == ATT_OK, "OTA status should be readable");
    require_int(g_ota_write_calls == 2u, "second OTA chunk should be written");
    require_int(g_ota_verify_calls == 1u, "complete OTA image should be verified");
    require_int(status.state == ATT_OTA_STATE_READY, "OTA should become ready after full CRC match");
    require_int(status.received_bytes == 3u, "OTA status should report all bytes received");
    require_int(status.actual_crc32 == image_crc, "OTA status should report actual CRC");
    require_int(memcmp(g_ota_image, image, sizeof(image)) == 0, "OTA bytes should be cached in order");
}

static void test_ota_query_rejects_wrong_advertised_slot(void)
{
    reset_mocks();
    att_device_config_t config = base_config();
    require_int(att_network_init(&config) == ATT_OK, "network init should succeed before OTA query");

    uint32_t wrong_slot = ATT_OTA_TARGET_SLOT_ID == 2u ? 1u : 2u;
    char wrong_slot_char = wrong_slot == 2u ? 'B' : 'A';

    require_int(att_network_query_ota() == ATT_OK, "OTA query should send request");

    char response[128];
    snprintf(response, sizeof(response), "OTA:VERSION=v2|SIZE=3|CRC32=12345678|CHUNK=2|SLOT=%c\n",
             wrong_slot_char);
    g_data_cb((const uint8_t *)response, (uint16_t)strlen(response), g_data_cb_ctx);

    att_ota_status_t status;
    require_int(att_network_get_ota_status(&status) == ATT_OK, "OTA status should be readable");
    require_int(status.state == ATT_OTA_STATE_ERROR, "wrong slot OTA advert should be rejected");
    require_int(g_ota_begin_calls == 0u, "wrong slot OTA advert should not create local image file");
}

int main(void)
{
    test_network_init_rejects_host_that_does_not_fit_esp_config();
    test_network_init_copies_valid_config_to_esp_driver();
    test_upload_ack_marks_matching_record_uploaded();
    test_upload_ack_ignores_invalid_sequence();
    test_upload_ack_handles_split_lines();
    test_upload_ack_handles_prefixed_ipd_line();
    test_batch_upload_sends_multiple_records_and_ack_marks_each();
    test_blacklist_response_updates_lookup();
    test_heartbeat_sends_live_temperature();
    test_network_status_reports_runtime_state();
    test_crc_wrapped_ack_is_verified_before_marking_upload();
    test_weather_query_returns_compact_oled_text();
    test_ota_query_downloads_chunks_and_verifies_image();
    test_ota_query_rejects_wrong_advertised_slot();
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

void ESP01S_RegisterDataCb(ESP01S_DataCallback_t pCb, void *pUserCtx)
{
    g_data_cb = pCb;
    g_data_cb_ctx = pUserCtx;
}

void ESP01S_SyncNtpTime(void)
{
}

uint8_t ESP01S_IsNtpSynced(void)
{
    return g_ntp_synced;
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
    g_weather_calls++;
    if (outTextDay != NULL && textDayBufSize > 0u) {
        snprintf(outTextDay, textDayBufSize, "%s", "Sunny");
    }
    if (outHigh != NULL && highBufSize > 0u) {
        snprintf(outHigh, highBufSize, "%s", "28");
    }
    if (outTextNight != NULL && textNightBufSize > 0u) {
        snprintf(outTextNight, textNightBufSize, "%s", "Clear");
    }
    if (outLow != NULL && lowBufSize > 0u) {
        snprintf(outLow, lowBufSize, "%s", "20");
    }
    if (outPrecip != NULL && precipBufSize > 0u) {
        snprintf(outPrecip, precipBufSize, "%s", "0");
    }
    return 0;
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_NOT_READY;
}

att_status_t att_storage_pending_uploads(att_record_t *records, uint8_t max_records, uint8_t *count)
{
    if (records == NULL || count == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_pending_status != ATT_OK) {
        *count = 0u;
        return g_pending_status;
    }
    uint8_t copied = g_pending_count < max_records ? g_pending_count : max_records;
    for (uint8_t i = 0u; i < copied; ++i) {
        records[i] = g_pending_records[i];
    }
    *count = copied;
    return copied == 0u ? ATT_ERR_NOT_READY : ATT_OK;
}

att_status_t att_storage_mark_uploaded(uint32_t seq)
{
    g_last_mark_uploaded_seq = seq;
    if (g_mark_uploaded_calls < (sizeof(g_mark_uploaded_seqs) / sizeof(g_mark_uploaded_seqs[0]))) {
        g_mark_uploaded_seqs[g_mark_uploaded_calls] = seq;
    }
    g_mark_uploaded_calls++;
    return ATT_OK;
}

att_status_t att_storage_ota_begin(const char *version, uint32_t size_bytes, uint32_t expected_crc32)
{
    g_ota_begin_calls++;
    if (version != NULL) {
        snprintf(g_ota_begin_version, sizeof(g_ota_begin_version), "%s", version);
    }
    g_ota_begin_size = size_bytes;
    g_ota_begin_crc32 = expected_crc32;
    return g_ota_begin_status;
}

att_status_t att_storage_ota_write(uint32_t offset, const uint8_t *data, size_t len)
{
    if (data == NULL || offset + len > sizeof(g_ota_image)) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_ota_write_status != ATT_OK) {
        return g_ota_write_status;
    }
    memcpy(g_ota_image + offset, data, len);
    g_ota_write_calls++;
    return ATT_OK;
}

att_status_t att_storage_ota_mark_verified(uint32_t actual_crc32)
{
    (void)actual_crc32;
    return ATT_OK;
}

att_status_t att_storage_ota_status(att_ota_file_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_ota_status_status != ATT_OK) {
        return g_ota_status_status;
    }
    *status = g_cached_ota;
    return ATT_OK;
}

att_status_t att_storage_ota_verify(uint32_t *actual_crc32)
{
    if (actual_crc32 == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_ota_verify_status != ATT_OK) {
        return g_ota_verify_status;
    }
    g_ota_verify_calls++;
    *actual_crc32 = g_ota_actual_crc32;
    return ATT_OK;
}

att_status_t att_temperature_read_centi_c(int16_t *centi_c)
{
    if (centi_c == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_temperature_status != ATT_OK) {
        return g_temperature_status;
    }
    *centi_c = g_temperature_centi_c;
    return ATT_OK;
}

ESP01S_State_t ESP01S_GetState(void)
{
    return g_esp_state;
}
