#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attendance_app.h"
#include "att_card.h"
#include "att_storage.h"

typedef struct {
    char text[512];
} send_capture_t;

static att_record_t g_appended_records[4];
static uint32_t g_appended_record_count;
static uint32_t g_now_sec = 1234u;
static attendance_feedback_event_t g_feedback_events[8];
static uint32_t g_feedback_count;

static void capture_send(const char *line, void *ctx)
{
    send_capture_t *capture = (send_capture_t *)ctx;
    strncat(capture->text, line, sizeof(capture->text) - strlen(capture->text) - 1u);
}

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

static void capture_feedback(attendance_feedback_event_t event, void *ctx)
{
    (void)ctx;
    if (g_feedback_count < (sizeof(g_feedback_events) / sizeof(g_feedback_events[0]))) {
        g_feedback_events[g_feedback_count++] = event;
    }
}

static void reset_serial_test_state(void)
{
    memset(g_appended_records, 0, sizeof(g_appended_records));
    g_appended_record_count = 0u;
    memset(g_feedback_events, 0, sizeof(g_feedback_events));
    g_feedback_count = 0u;
    g_now_sec = 1234u;
    require_int(attendance_app_init() == ATT_OK, "attendance_app_init should succeed");
    attendance_app_set_time_source(fake_now, NULL);
    attendance_app_set_feedback(capture_feedback, NULL);
}

static void test_dispatches_split_line(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    attendance_app_dispatch_serial_bytes((const uint8_t *)"PI", 2u);
    require_int(capture.text[0] == '\0', "partial line should not dispatch");

    attendance_app_dispatch_serial_bytes((const uint8_t *)"NG\n", 3u);
    require_int(strcmp(capture.text, "OK:PONG\n") == 0, "complete line should dispatch");
}

static void test_trims_crlf_and_ignores_empty_lines(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    attendance_app_dispatch_serial_bytes((const uint8_t *)"PING\r\n\n", 7u);
    require_int(strcmp(capture.text, "OK:PONG\n") == 0, "CRLF plus empty line should dispatch once");
}

static void test_discards_overlong_line(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    uint8_t command[ATT_SERIAL_LINE_MAX + 16u];
    memset(command, 'A', sizeof(command));
    command[sizeof(command) - 1u] = '\n';

    attendance_app_dispatch_serial_bytes(command, sizeof(command));
    attendance_app_dispatch_serial_bytes((const uint8_t *)"PING\n", 5u);

    require_int(strcmp(capture.text, "ERR:ARG\nOK:PONG\n") == 0,
                "overlong line should be rejected and next line should recover");
}

static void test_simatt_appends_pending_record(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    const char *command = "SIMATT:A1B2C3D4,1001,2\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)command, strlen(command));

    require_int(strcmp(capture.text, "OK:SIMATT:SEQ=1\n") == 0,
                "SIMATT should acknowledge generated sequence");
    require_int(g_appended_record_count == 1u, "SIMATT should append one record");
    require_int(g_appended_records[0].seq == 1u, "SIMATT should assign next sequence");
    require_int(g_appended_records[0].uid.bytes[0] == 0xA1 &&
                g_appended_records[0].uid.bytes[3] == 0xD4,
                "SIMATT should parse UID");
    require_int(g_appended_records[0].sid == 1001u, "SIMATT should parse SID");
    require_int(g_appended_records[0].type == ATT_RECORD_NORMAL, "SIMATT should parse record type");
    require_int(g_appended_records[0].timestamp == g_now_sec, "SIMATT should use app time");
    require_int(g_appended_records[0].device_id == 1u, "SIMATT should use device id");
    require_int(g_appended_records[0].upload_state == ATT_UPLOAD_PENDING,
                "SIMATT should create pending upload");
}

static void test_simatt_rejects_bad_arguments(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    const char *command = "SIMATT:BAD,1001,2\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)command, strlen(command));

    require_int(strcmp(capture.text, "ERR:ARG\n") == 0, "bad SIMATT should return arg error");
    require_int(g_appended_record_count == 0u, "bad SIMATT should not append record");
}

static void test_uitest_dispatches_feedback_cases(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    const char *commands =
        "UITEST:OK\n"
        "UITEST:DUP\n"
        "UITEST:INVALID\n"
        "UITEST:ERROR\n"
        "UITEST:NETOK\n"
        "UITEST:NETERR\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)commands, strlen(commands));

    require_int(strcmp(capture.text,
                       "OK:UITEST\n"
                       "OK:UITEST\n"
                       "OK:UITEST\n"
                       "OK:UITEST\n"
                       "OK:UITEST\n"
                       "OK:UITEST\n") == 0,
                "UITEST cases should acknowledge success");
    require_int(g_feedback_count == 6u, "UITEST cases should emit feedback events");
    require_int(g_feedback_events[0] == ATT_FEEDBACK_ATTEND_OK, "UITEST OK should emit OK feedback");
    require_int(g_feedback_events[1] == ATT_FEEDBACK_ATTEND_DUPLICATE,
                "UITEST DUP should emit duplicate feedback");
    require_int(g_feedback_events[2] == ATT_FEEDBACK_CARD_INVALID,
                "UITEST INVALID should emit invalid-card feedback");
    require_int(g_feedback_events[3] == ATT_FEEDBACK_ERROR, "UITEST ERROR should emit error feedback");
    require_int(g_feedback_events[4] == ATT_FEEDBACK_NETWORK_ONLINE,
                "UITEST NETOK should emit network-online feedback");
    require_int(g_feedback_events[5] == ATT_FEEDBACK_NETWORK_FAULT,
                "UITEST NETERR should emit network-fault feedback");
}

static void test_uitest_rejects_bad_case(void)
{
    reset_serial_test_state();
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    const char *command = "UITEST:BAD\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)command, strlen(command));

    require_int(strcmp(capture.text, "ERR:ARG\n") == 0, "bad UITEST should return arg error");
    require_int(g_feedback_count == 0u, "bad UITEST should not emit feedback");
}

int main(void)
{
    test_dispatches_split_line();
    test_trims_crlf_and_ignores_empty_lines();
    test_discards_overlong_line();
    test_simatt_appends_pending_record();
    test_simatt_rejects_bad_arguments();
    test_uitest_dispatches_feedback_cases();
    test_uitest_rejects_bad_case();
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
    memset(uid, 0, sizeof(*uid));
    return ATT_ERR_NO_CARD;
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
    att_storage_default_config(config);
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
    if (record == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (g_appended_record_count >= 4u) {
        return ATT_ERR_STORAGE;
    }
    g_appended_records[g_appended_record_count++] = *record;
    return ATT_OK;
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
    return ATT_OK;
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_NOT_READY;
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
    return text == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}
