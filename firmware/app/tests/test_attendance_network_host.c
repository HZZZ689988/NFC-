#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attendance_app.h"
#include "att_card.h"
#include "att_network.h"
#include "att_storage.h"

static att_device_config_t g_config;
static uint32_t g_now_sec;
static unsigned g_network_init_calls;
static unsigned g_upload_calls;
static unsigned g_heartbeat_calls;
static unsigned g_blacklist_query_calls;
static unsigned g_time_sync_calls;
static unsigned g_weather_calls;
static unsigned g_weather_save_calls;
static unsigned g_mark_uploaded_calls;
static unsigned g_event_count;
static unsigned g_upload_event;
static unsigned g_weather_event;
static unsigned g_ota_query_calls;
static unsigned g_reset_calls;
static att_status_t g_network_init_status;
static att_status_t g_upload_status;
static att_status_t g_weather_status;
static att_network_status_t g_network_status;
static att_ota_status_t g_ota_status;
static att_ota_file_status_t g_ota_cache;
static att_status_t g_ota_cache_status;

typedef struct {
    char text[512];
} send_capture_t;

static void require_int(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static void capture_send(const char *line, void *ctx)
{
    send_capture_t *capture = (send_capture_t *)ctx;
    strncat(capture->text, line, sizeof(capture->text) - strlen(capture->text) - 1u);
}

static void capture_reset(void *ctx)
{
    (void)ctx;
    g_reset_calls++;
}

static uint32_t fake_now(void *ctx)
{
    (void)ctx;
    return g_now_sec;
}

static void reset_mocks(void)
{
    memset(&g_config, 0, sizeof(g_config));
    g_config.device_id = 7u;
    g_config.upload_enable = 1u;
    g_config.work_mode = ATT_MODE_IN_OUT;
    g_config.repeat_interval_sec = 60u;
    g_config.server_port = 9000u;
    g_config.timezone = 8;
    snprintf(g_config.server_host, sizeof(g_config.server_host), "%s", "192.168.1.10");

    g_now_sec = 1000u;
    g_network_init_calls = 0u;
    g_upload_calls = 0u;
    g_heartbeat_calls = 0u;
    g_blacklist_query_calls = 0u;
    g_time_sync_calls = 0u;
    g_weather_calls = 0u;
    g_weather_save_calls = 0u;
    g_mark_uploaded_calls = 0u;
    g_event_count = 0u;
    g_upload_event = 0u;
    g_weather_event = 0u;
    g_ota_query_calls = 0u;
    g_reset_calls = 0u;
    g_network_init_status = ATT_OK;
    g_upload_status = ATT_OK;
    g_weather_status = ATT_OK;
    memset(&g_network_status, 0, sizeof(g_network_status));
    g_network_status.esp_state = 6u;
    g_network_status.ntp_synced = 1u;
    g_network_status.blacklist_count = 2u;
    memset(&g_ota_status, 0, sizeof(g_ota_status));
    g_ota_status.state = ATT_OTA_STATE_IDLE;
    memset(&g_ota_cache, 0, sizeof(g_ota_cache));
    g_ota_cache_status = ATT_ERR_STORAGE;
}

static void init_app(void)
{
    att_status_t status = attendance_app_init();
    require_int(status == ATT_OK, "attendance_app_init should succeed");
    attendance_app_set_time_source(fake_now, NULL);
}

static void test_network_poll_schedules_heartbeat_and_upload(void)
{
    reset_mocks();
    init_app();

    attendance_app_poll_network();
    require_int(g_network_init_calls == 1u, "network should initialize from loaded config");
    require_int(g_time_sync_calls == 1u, "first network poll should sync time");
    require_int(g_heartbeat_calls == 1u, "first network poll should send heartbeat");
    require_int(g_blacklist_query_calls == 1u, "first heartbeat should query blacklist");
    require_int(g_upload_calls == 1u, "first network poll should attempt pending upload");
    require_int(g_weather_calls == 0u, "upload-busy poll should skip weather query");
    require_int(g_weather_save_calls == 0u, "skipped weather should not cache text");
    require_int(g_mark_uploaded_calls == 0u, "upload should stay pending without ACK parsing");

    attendance_app_poll_network();
    require_int(g_heartbeat_calls == 1u, "heartbeat should not repeat before interval");
    require_int(g_blacklist_query_calls == 1u, "blacklist should not repeat before heartbeat interval");
    require_int(g_upload_calls == 1u, "upload should not repeat before interval");
    require_int(g_time_sync_calls == 1u, "time sync should not repeat before interval");
    require_int(g_weather_calls == 1u, "deferred weather should run when upload is idle");
    require_int(g_weather_save_calls == 1u, "deferred weather query should cache text");
    require_int(g_upload_event < g_weather_event, "upload should be prioritized before weather");

    g_now_sec += 10u;
    attendance_app_poll_network();
    require_int(g_heartbeat_calls == 1u, "heartbeat interval should be longer than upload interval");
    require_int(g_upload_calls == 2u, "upload should retry after upload interval");
    require_int(g_weather_calls == 1u, "upload retry should keep weather deferred");

    g_now_sec += 50u;
    attendance_app_poll_network();
    require_int(g_heartbeat_calls == 2u, "heartbeat should repeat after heartbeat interval");
    require_int(g_blacklist_query_calls == 2u, "blacklist should refresh after heartbeat interval");
    require_int(g_upload_calls == 3u, "upload should continue retrying pending records");
    require_int(g_mark_uploaded_calls == 0u, "polling should not mark uploads done without ACK");
}

static void test_network_poll_is_disabled_when_upload_disabled(void)
{
    reset_mocks();
    g_config.upload_enable = 0u;
    init_app();

    attendance_app_poll_network();

    require_int(g_network_init_calls == 1u, "network config init may still run");
    require_int(g_time_sync_calls == 1u, "disabled upload should still sync time");
    require_int(g_weather_calls == 1u, "disabled upload should still query weather");
    require_int(g_weather_save_calls == 1u, "disabled upload should still cache weather");
    require_int(g_heartbeat_calls == 0u, "disabled upload should skip heartbeat");
    require_int(g_blacklist_query_calls == 0u, "disabled upload should skip blacklist query");
    require_int(g_upload_calls == 0u, "disabled upload should skip upload");
}

static void test_network_poll_queries_weather_when_no_upload_pending(void)
{
    reset_mocks();
    g_upload_status = ATT_ERR_NOT_READY;
    init_app();

    attendance_app_poll_network();

    require_int(g_upload_calls == 1u, "upload scan should still run");
    require_int(g_weather_calls == 1u, "weather should run when no upload is pending");
    require_int(g_weather_save_calls == 1u, "weather should be cached when no upload is pending");
}

static void test_weather_failure_does_not_block_later_upload(void)
{
    reset_mocks();
    g_upload_status = ATT_ERR_NOT_READY;
    g_weather_status = ATT_ERR;
    init_app();

    attendance_app_poll_network();
    require_int(g_upload_calls == 1u, "first poll should scan pending uploads");
    require_int(g_weather_calls == 1u, "weather should run when upload is idle");
    require_int(g_weather_save_calls == 0u, "failed weather should not be cached");

    g_upload_status = ATT_OK;
    g_now_sec += 10u;
    attendance_app_poll_network();

    require_int(g_upload_calls == 2u, "later upload should run after weather failure");
    require_int(g_weather_calls == 1u, "upload-busy retry should skip weather");
}

static void test_pending_upload_retries_after_app_reinit(void)
{
    reset_mocks();
    init_app();

    attendance_app_poll_network();
    require_int(g_upload_calls == 1u, "first boot should attempt pending upload");
    require_int(g_mark_uploaded_calls == 0u, "pending upload should not be marked done without ACK");

    init_app();
    attendance_app_poll_network();

    require_int(g_network_init_calls == 2u, "reinit should initialize network again");
    require_int(g_upload_calls == 2u, "pending upload should retry after reinit");
    require_int(g_mark_uploaded_calls == 0u, "reinit should still wait for ACK before marking done");
}

static void test_network_poll_skips_when_network_init_fails(void)
{
    reset_mocks();
    g_network_init_status = ATT_ERR_INVALID_ARG;
    init_app();

    attendance_app_poll_network();

    require_int(g_network_init_calls == 1u, "network init should be attempted");
    require_int(g_heartbeat_calls == 0u, "failed network init should skip heartbeat");
    require_int(g_upload_calls == 0u, "failed network init should skip upload");
}

static void test_serial_net_query_reports_link_status(void)
{
    reset_mocks();
    init_app();
    g_network_status.esp_state = 2u;
    g_network_status.ntp_synced = 0u;
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    const char *command = "NET?\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)command, strlen(command));

    require_int(strcmp(capture.text,
                       "NET:READY=0|CFG=1|UPLOAD=1|STATE=2|TEXT=WIFI_CONNECTING|NTP=0|BL=2|DEV=7|HOST=192.168.1.10|PORT=9000|SSID=\n") == 0,
                "NET? should report runtime network status");
}

static void test_serial_ota_commands_report_and_trigger_query(void)
{
    reset_mocks();
    init_app();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);
    g_ota_status.state = ATT_OTA_STATE_READY;
    snprintf(g_ota_status.version, sizeof(g_ota_status.version), "%s", "v1");
    g_ota_status.size_bytes = 3u;
    g_ota_status.received_bytes = 3u;
    g_ota_status.expected_crc32 = 0x55AA1122u;
    g_ota_status.actual_crc32 = 0x55AA1122u;

    const char *status_command = "OTA?\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)status_command, strlen(status_command));

    require_int(strcmp(capture.text,
                       "OTA:STATE=READY|VER=v1|SLOT=-|RX=3|SIZE=3|CRC32=55AA1122|ACT=55AA1122\n") == 0,
                "OTA? should report OTA network status");

    capture.text[0] = '\0';
    const char *query_command = "OTA!\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)query_command, strlen(query_command));

    require_int(strcmp(capture.text, "OK:OTA\n") == 0, "OTA! should acknowledge accepted query");
    require_int(g_ota_query_calls == 1u, "OTA! should trigger network OTA query");
}

static void test_serial_otarst_requires_verified_pending_cache(void)
{
    reset_mocks();
    init_app();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);
    attendance_app_set_reset(capture_reset, NULL);

    const char *not_ready_command = "OTARST\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)not_ready_command, strlen(not_ready_command));
    require_int(strcmp(capture.text, "ERR:OTA_NOT_READY\n") == 0,
                "OTARST should reject when OTA cache is absent");
    require_int(g_reset_calls == 0u, "OTARST should not reset without installable cache");

    capture.text[0] = '\0';
    g_ota_cache_status = ATT_OK;
    g_ota_cache.valid = 1u;
    g_ota_cache.verified = 1u;
    snprintf(g_ota_cache.version, sizeof(g_ota_cache.version), "%s", "v2");
    g_ota_cache.size_bytes = 1024u;
    g_ota_cache.received_bytes = 1024u;
    g_ota_cache.expected_crc32 = 0xAABBCCDDu;
    g_ota_cache.actual_crc32 = 0xAABBCCDDu;
    g_ota_cache.target_addr = ATT_APP_SLOT_BASE_ADDR;
    g_ota_cache.target_slot = ATT_OTA_TARGET_SLOT_ID;
    g_ota_cache.install_state = ATT_OTA_INSTALL_PENDING;
    g_ota_cache.install_error = ATT_OTA_INSTALL_ERR_NONE;

    const char *ready_command = "OTARST\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)ready_command, strlen(ready_command));
    require_int(strcmp(capture.text, "OK:OTARST\n") == 0,
                "OTARST should acknowledge installable cache");
    require_int(g_reset_calls == 1u, "OTARST should invoke reset callback for installable cache");
}

int main(void)
{
    test_network_poll_schedules_heartbeat_and_upload();
    test_network_poll_is_disabled_when_upload_disabled();
    test_network_poll_queries_weather_when_no_upload_pending();
    test_weather_failure_does_not_block_later_upload();
    test_pending_upload_retries_after_app_reinit();
    test_network_poll_skips_when_network_init_fails();
    test_serial_net_query_reports_link_status();
    test_serial_ota_commands_report_and_trigger_query();
    test_serial_otarst_requires_verified_pending_cache();
    return 0;
}

att_status_t att_card_init(void)
{
    return ATT_OK;
}

att_status_t att_card_read_uid(att_uid_t *uid)
{
    return uid == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_NO_CARD;
}

att_status_t att_card_read_person(att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(person, 0, sizeof(*person));
    return ATT_ERR_NO_CARD;
}

att_status_t att_card_diag(att_card_diag_t *diag)
{
    if (diag == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(diag, 0, sizeof(*diag));
    return ATT_OK;
}

att_status_t att_card_issue_checked(const att_person_t *person)
{
    return person == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_card_clear_checked(const att_uid_t *expected_uid)
{
    return expected_uid == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_card_write_image_block(att_card_image_area_t area, uint8_t index, const uint8_t data[16])
{
    (void)area;
    (void)index;
    return data == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_card_finish_image_update(void)
{
    return ATT_OK;
}

att_status_t att_storage_init(void)
{
    return ATT_OK;
}

att_status_t att_storage_format(void)
{
    return ATT_OK;
}

att_status_t att_storage_load_config(att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    *config = g_config;
    return ATT_OK;
}

att_status_t att_storage_save_config(const att_device_config_t *config)
{
    return config == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

void att_storage_default_config(att_device_config_t *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
        config->device_id = 1u;
        config->work_mode = ATT_MODE_IN_OUT;
        config->repeat_interval_sec = 60u;
    }
}

att_status_t att_storage_append_record(const att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_storage_read_record(uint32_t index, att_record_t *record)
{
    (void)index;
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_STORAGE;
}

att_status_t att_storage_record_count(uint32_t *count)
{
    if (count == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    *count = 0u;
    return ATT_OK;
}

att_status_t att_storage_mark_uploaded(uint32_t seq)
{
    (void)seq;
    g_mark_uploaded_calls++;
    return ATT_OK;
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    if (record == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(record, 0, sizeof(*record));
    record->seq = 1u;
    record->uid.bytes[0] = 0xA1;
    record->uid.bytes[1] = 0xB2;
    record->uid.bytes[2] = 0xC3;
    record->uid.bytes[3] = 0xD4;
    record->sid = 1001u;
    record->type = ATT_RECORD_NORMAL;
    record->timestamp = 1000u;
    record->device_id = g_config.device_id;
    record->upload_state = ATT_UPLOAD_PENDING;
    return ATT_OK;
}

att_status_t att_storage_load_weather(char *text, size_t text_len)
{
    if (text == NULL || text_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    text[0] = '\0';
    return ATT_ERR_STORAGE;
}

att_status_t att_storage_save_weather(const char *text)
{
    if (text == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_weather_save_calls++;
    return ATT_OK;
}

att_status_t att_storage_ota_status(att_ota_file_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_ota_cache_status != ATT_OK) {
        return g_ota_cache_status;
    }
    *status = g_ota_cache;
    return ATT_OK;
}

att_status_t att_storage_boot_state_load(att_boot_state_t *state)
{
    if (state == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(state, 0, sizeof(*state));
    return ATT_OK;
}

att_status_t att_storage_boot_state_save(att_boot_state_t *state)
{
    return state == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_storage_boot_confirm_current(void)
{
    return ATT_OK;
}

att_status_t att_network_init(const att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_network_init_calls++;
    if (g_network_init_status == ATT_OK) {
        g_network_status.configured = 1u;
        g_network_status.upload_enabled = config->upload_enable ? 1u : 0u;
        g_network_status.device_id = config->device_id;
        g_network_status.server_port = config->server_port;
        snprintf(g_network_status.ssid, sizeof(g_network_status.ssid), "%s", config->wifi_ssid);
        snprintf(g_network_status.server_host, sizeof(g_network_status.server_host), "%s", config->server_host);
    }
    return g_network_init_status;
}

att_status_t att_network_sync_time(void)
{
    g_time_sync_calls++;
    return ATT_OK;
}

att_status_t att_network_query_weather(char *text, size_t text_len)
{
    g_event_count++;
    g_weather_event = g_event_count;
    g_weather_calls++;
    if (text != NULL && text_len > 0u) {
        snprintf(text, text_len, "Hangzhou Sunny/20C");
    }
    return g_weather_status;
}

att_status_t att_network_upload_pending(void)
{
    g_event_count++;
    g_upload_event = g_event_count;
    g_upload_calls++;
    return g_config.upload_enable ? g_upload_status : ATT_ERR_NOT_READY;
}

att_status_t att_network_upload_pending_batch(uint8_t max_records)
{
    (void)max_records;
    g_event_count++;
    g_upload_event = g_event_count;
    g_upload_calls++;
    return g_config.upload_enable ? g_upload_status : ATT_ERR_NOT_READY;
}

att_status_t att_network_send_heartbeat(void)
{
    g_heartbeat_calls++;
    return g_config.upload_enable ? ATT_OK : ATT_ERR_NOT_READY;
}

att_status_t att_network_query_blacklist(void)
{
    g_blacklist_query_calls++;
    return g_config.upload_enable ? ATT_OK : ATT_ERR_NOT_READY;
}

att_status_t att_network_query_ota(void)
{
    g_ota_query_calls++;
    return ATT_OK;
}

att_status_t att_network_get_ota_status(att_ota_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    *status = g_ota_status;
    return ATT_OK;
}

uint8_t att_network_uid_is_blacklisted(const att_uid_t *uid)
{
    (void)uid;
    return 0u;
}

att_status_t att_network_get_status(att_network_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    *status = g_network_status;
    return ATT_OK;
}
