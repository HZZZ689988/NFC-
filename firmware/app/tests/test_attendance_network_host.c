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
static unsigned g_mark_uploaded_calls;
static att_status_t g_network_init_status;

static void require_int(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
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
    g_mark_uploaded_calls = 0u;
    g_network_init_status = ATT_OK;
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
    require_int(g_heartbeat_calls == 1u, "first network poll should send heartbeat");
    require_int(g_upload_calls == 1u, "first network poll should attempt pending upload");
    require_int(g_mark_uploaded_calls == 0u, "upload should stay pending without ACK parsing");

    attendance_app_poll_network();
    require_int(g_heartbeat_calls == 1u, "heartbeat should not repeat before interval");
    require_int(g_upload_calls == 1u, "upload should not repeat before interval");

    g_now_sec += 10u;
    attendance_app_poll_network();
    require_int(g_heartbeat_calls == 1u, "heartbeat interval should be longer than upload interval");
    require_int(g_upload_calls == 2u, "upload should retry after upload interval");

    g_now_sec += 50u;
    attendance_app_poll_network();
    require_int(g_heartbeat_calls == 2u, "heartbeat should repeat after heartbeat interval");
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
    require_int(g_heartbeat_calls == 0u, "disabled upload should skip heartbeat");
    require_int(g_upload_calls == 0u, "disabled upload should skip upload");
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

int main(void)
{
    test_network_poll_schedules_heartbeat_and_upload();
    test_network_poll_is_disabled_when_upload_disabled();
    test_network_poll_skips_when_network_init_fails();
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

att_status_t att_card_issue_checked(const att_person_t *person)
{
    return person == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_card_clear_checked(const att_uid_t *expected_uid)
{
    return expected_uid == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
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

att_status_t att_network_init(const att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_network_init_calls++;
    return g_network_init_status;
}

att_status_t att_network_sync_time(void)
{
    return ATT_OK;
}

att_status_t att_network_query_weather(void)
{
    return ATT_OK;
}

att_status_t att_network_upload_pending(void)
{
    g_upload_calls++;
    return g_config.upload_enable ? ATT_OK : ATT_ERR_NOT_READY;
}

att_status_t att_network_send_heartbeat(void)
{
    g_heartbeat_calls++;
    return g_config.upload_enable ? ATT_OK : ATT_ERR_NOT_READY;
}
