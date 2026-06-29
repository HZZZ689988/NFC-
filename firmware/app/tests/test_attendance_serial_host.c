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

static void test_dispatches_split_line(void)
{
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    attendance_app_dispatch_serial_bytes((const uint8_t *)"PI", 2u);
    require_int(capture.text[0] == '\0', "partial line should not dispatch");

    attendance_app_dispatch_serial_bytes((const uint8_t *)"NG\n", 3u);
    require_int(strcmp(capture.text, "OK:PONG\n") == 0, "complete line should dispatch");
}

static void test_trims_crlf_and_ignores_empty_lines(void)
{
    send_capture_t capture = {0};
    attendance_app_set_serial_send(capture_send, &capture);

    attendance_app_dispatch_serial_bytes((const uint8_t *)"PING\r\n\n", 7u);
    require_int(strcmp(capture.text, "OK:PONG\n") == 0, "CRLF plus empty line should dispatch once");
}

static void test_discards_overlong_line(void)
{
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

int main(void)
{
    test_dispatches_split_line();
    test_trims_crlf_and_ignores_empty_lines();
    test_discards_overlong_line();
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
    return ATT_OK;
}

att_status_t att_storage_next_pending_upload(att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_ERR_NOT_READY;
}
