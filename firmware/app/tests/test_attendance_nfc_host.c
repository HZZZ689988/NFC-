#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attendance_app.h"
#include "att_card.h"
#include "att_storage.h"

typedef struct {
    char text[512];
} send_capture_t;

static att_device_config_t g_config;
static att_uid_t g_card_uid = {{0xA1, 0xB2, 0xC3, 0xD4}};
static att_status_t g_card_status = ATT_OK;
static att_status_t g_append_status = ATT_OK;
static att_status_t g_read_person_status = ATT_OK;
static att_record_t g_last_record;
static uint32_t g_record_count;
static uint32_t g_append_calls;
static uint32_t g_now_sec;

static void capture_send(const char *line, void *ctx)
{
    send_capture_t *capture = (send_capture_t *)ctx;
    strncat(capture->text, line, sizeof(capture->text) - strlen(capture->text) - 1u);
}

static uint32_t fake_now(void *ctx)
{
    (void)ctx;
    return g_now_sec;
}

static void require_int(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static void init_app(send_capture_t *capture)
{
    att_storage_default_config(&g_config);
    g_config.repeat_interval_sec = 60u;
    g_config.device_id = 42u;
    g_config.work_mode = ATT_MODE_IN_OUT;

    att_status_t status = attendance_app_init();
    require_int(status == ATT_OK, "attendance_app_init should succeed");
    attendance_app_set_serial_send(capture_send, capture);
    attendance_app_set_time_source(fake_now, NULL);
}

static void reset_mocks(void)
{
    memset(&g_config, 0, sizeof(g_config));
    memset(&g_last_record, 0, sizeof(g_last_record));
    g_card_uid.bytes[0] = 0xA1;
    g_card_uid.bytes[1] = 0xB2;
    g_card_uid.bytes[2] = 0xC3;
    g_card_uid.bytes[3] = 0xD4;
    g_card_status = ATT_OK;
    g_append_status = ATT_OK;
    g_read_person_status = ATT_OK;
    g_record_count = 0u;
    g_append_calls = 0u;
    g_now_sec = 100u;
}

static void test_records_first_card(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();

    require_int(g_append_calls == 1u, "first card should append one record");
    require_int(g_last_record.seq == 1u, "first record seq should start at one");
    require_int(g_last_record.timestamp == 100u, "record should use configured time source");
    require_int(g_last_record.device_id == 42u, "record should use loaded device id");
    require_int(g_last_record.sid == 1001u, "record should use card account SID");
    require_int(g_last_record.uid.bytes[0] == 0xA1 && g_last_record.uid.bytes[3] == 0xD4,
                "record should contain read UID");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=1\n") == 0,
                "first card should log append success");
}

static void test_skips_same_uid_inside_repeat_interval(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';
    g_now_sec = 130u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 1u, "duplicate card should not append");
    require_int(strcmp(capture.text, "ATTEND:SKIP:DUPLICATE\n") == 0,
                "duplicate card should log skip");
}

static void test_records_same_uid_after_repeat_interval(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';
    g_now_sec = 160u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 2u, "same card after repeat interval should append");
    require_int(g_last_record.seq == 2u, "second accepted record should increment seq");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=2\n") == 0,
                "second accepted record should log success");
}

static void test_no_card_is_silent(void)
{
    reset_mocks();
    g_card_status = ATT_ERR_NO_CARD;
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();

    require_int(g_append_calls == 0u, "no-card should not append");
    require_int(capture.text[0] == '\0', "no-card should not log");
}

static void test_invalid_card_is_reported_without_append(void)
{
    reset_mocks();
    g_read_person_status = ATT_ERR_CRC;
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();

    require_int(g_append_calls == 0u, "invalid card should not append");
    require_int(strcmp(capture.text, "ATTEND:ERR:INVALID_CARD\n") == 0,
                "invalid card should log explicit invalid-card error");
}

static void test_storage_error_is_reported_without_consuming_seq(void)
{
    reset_mocks();
    g_append_status = ATT_ERR_STORAGE;
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    require_int(g_append_calls == 1u, "storage error still attempts append");
    require_int(strcmp(capture.text, "ATTEND:ERR:STORAGE\n") == 0,
                "storage error should log");

    capture.text[0] = '\0';
    g_append_status = ATT_OK;
    g_now_sec = 101u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 2u, "retry after storage error should append again");
    require_int(g_last_record.seq == 1u, "failed append should not consume seq");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=1\n") == 0,
                "retry should log success with original seq");
}

int main(void)
{
    test_records_first_card();
    test_skips_same_uid_inside_repeat_interval();
    test_records_same_uid_after_repeat_interval();
    test_no_card_is_silent();
    test_invalid_card_is_reported_without_append();
    test_storage_error_is_reported_without_consuming_seq();
    return 0;
}

att_status_t att_card_init(void)
{
    return ATT_OK;
}

att_status_t att_card_read_uid(att_uid_t *uid)
{
    if (uid == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    *uid = g_card_uid;
    return g_card_status;
}

att_status_t att_card_read_person(att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(person, 0, sizeof(*person));
    person->uid = g_card_uid;
    person->sid = 1001u;
    person->points = 7u;
    person->card_type = ATT_CARD_NORMAL;
    if (g_read_person_status != ATT_OK) {
        return g_read_person_status;
    }
    return g_card_status;
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
        config->upload_enable = 1u;
        config->repeat_interval_sec = 60u;
    }
}

att_status_t att_storage_append_record(const att_record_t *record)
{
    if (record == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_last_record = *record;
    g_append_calls++;
    return g_append_status;
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
    *count = g_record_count;
    return ATT_OK;
}

att_status_t att_storage_mark_uploaded(uint32_t seq)
{
    (void)seq;
    return ATT_OK;
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_NOT_READY;
}
