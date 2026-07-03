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
static att_card_type_t g_card_type = ATT_CARD_NORMAL;
static att_record_t g_records[16];
static att_record_t g_last_record;
static uint32_t g_record_count;
static uint32_t g_append_calls;
static uint32_t g_read_record_calls;
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

static void init_app_with_mode(send_capture_t *capture, att_work_mode_t mode)
{
    att_storage_default_config(&g_config);
    g_config.repeat_interval_sec = 60u;
    g_config.device_id = 42u;
    g_config.work_mode = mode;

    att_status_t status = attendance_app_init();
    require_int(status == ATT_OK, "attendance_app_init should succeed");
    attendance_app_set_serial_send(capture_send, capture);
    attendance_app_set_time_source(fake_now, NULL);
}

static void init_app(send_capture_t *capture)
{
    init_app_with_mode(capture, ATT_MODE_IN_OUT);
}

static void reset_mocks(void)
{
    memset(&g_config, 0, sizeof(g_config));
    memset(g_records, 0, sizeof(g_records));
    memset(&g_last_record, 0, sizeof(g_last_record));
    g_card_uid.bytes[0] = 0xA1;
    g_card_uid.bytes[1] = 0xB2;
    g_card_uid.bytes[2] = 0xC3;
    g_card_uid.bytes[3] = 0xD4;
    g_card_status = ATT_OK;
    g_append_status = ATT_OK;
    g_read_person_status = ATT_OK;
    g_card_type = ATT_CARD_NORMAL;
    g_record_count = 0u;
    g_append_calls = 0u;
    g_read_record_calls = 0u;
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
    require_int(g_last_record.type == ATT_RECORD_IN, "first IN_OUT swipe should be an IN record");
    require_int(g_last_record.uid.bytes[0] == 0xA1 && g_last_record.uid.bytes[3] == 0xD4,
                "record should contain read UID");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=1|TYPE=IN\n") == 0,
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
    require_int(g_last_record.type == ATT_RECORD_OUT, "second IN_OUT swipe should be an OUT record");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=2|TYPE=OUT|DUR=60\n") == 0,
                "second accepted record should log success");
}

static void test_lru_cache_avoids_flash_scan_for_recent_uid(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';
    g_read_record_calls = 0u;
    g_now_sec = 160u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 2u, "second accepted swipe should append");
    require_int(g_last_record.type == ATT_RECORD_OUT, "cached latest IN should produce OUT");
    require_int(g_read_record_calls == 0u, "recent UID should use LRU cache instead of Flash scan");
}

static void test_next_seq_uses_latest_stored_record_after_init(void)
{
    reset_mocks();
    memset(&g_records[0], 0, sizeof(g_records[0]));
    g_records[0].seq = 9u;
    g_records[0].uid = g_card_uid;
    g_records[0].sid = 1001u;
    g_records[0].type = ATT_RECORD_OUT;
    g_records[0].timestamp = 90u;
    g_record_count = 1u;

    send_capture_t capture = {0};
    init_app(&capture);
    attendance_app_poll_nfc();

    require_int(g_append_calls == 1u, "existing storage should allow append");
    require_int(g_last_record.seq == 10u, "next seq should follow latest stored seq");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=10|TYPE=IN\n") == 0,
                "append after init should report seq 10");
}

static void test_in_out_mode_alternates_back_to_entry(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    g_now_sec = 160u;
    attendance_app_poll_nfc();
    capture.text[0] = '\0';
    g_now_sec = 220u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 3u, "third accepted swipe should append");
    require_int(g_last_record.seq == 3u, "third accepted swipe should increment seq");
    require_int(g_last_record.type == ATT_RECORD_IN, "third IN_OUT swipe should return to IN");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=3|TYPE=IN\n") == 0,
                "third IN_OUT swipe should log IN");
}

static void test_out_duration_supports_cross_day(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    g_now_sec = 86400u - 600u;
    init_app(&capture);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';
    g_now_sec = 86400u + 600u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 2u, "cross-day OUT should append");
    require_int(g_last_record.type == ATT_RECORD_OUT, "cross-day second swipe should be OUT");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=2|TYPE=OUT|DUR=1200\n") == 0,
                "cross-day OUT should report 1200 seconds duration");
}

static void test_check_in_mode_rejects_already_inside_card(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app_with_mode(&capture, ATT_MODE_CHECK_IN);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';
    g_now_sec = 160u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 1u, "already-in card should not append another IN");
    require_int(strcmp(capture.text, "ATTEND:ERR:ALREADY_IN\n") == 0,
                "already-in card should report logical duplicate");
}

static void test_check_out_mode_rejects_card_without_entry(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app_with_mode(&capture, ATT_MODE_CHECK_OUT);

    attendance_app_poll_nfc();

    require_int(g_append_calls == 0u, "no-entry OUT should not append");
    require_int(strcmp(capture.text, "ATTEND:ERR:NO_ENTRY\n") == 0,
                "no-entry OUT should be reported");
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
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=1|TYPE=IN\n") == 0,
                "retry should log success with original seq");
}

static void test_cardlock_pauses_nfc_poll(void)
{
    reset_mocks();
    send_capture_t capture = {0};
    init_app(&capture);

    const char *lock = "CARDLOCK:ON\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)lock, strlen(lock));
    capture.text[0] = '\0';

    attendance_app_poll_nfc();
    require_int(g_append_calls == 0u, "paused NFC poll should not append records");
    require_int(capture.text[0] == '\0', "paused NFC poll should not emit attendance lines");

    const char *unlock = "CARDLOCK:OFF\n";
    attendance_app_dispatch_serial_bytes((const uint8_t *)unlock, strlen(unlock));
    capture.text[0] = '\0';

    attendance_app_poll_nfc();
    require_int(g_append_calls == 1u, "unlocked NFC poll should append records again");
    require_int(strcmp(capture.text, "ATTEND:OK:SEQ=1|TYPE=IN\n") == 0,
                "unlocked NFC poll should log attendance success");
}

static void test_admin_card_enters_mode_and_denies_normal_card(void)
{
    reset_mocks();
    g_card_type = ATT_CARD_ADMIN;
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();

    require_int(g_append_calls == 0u, "admin card should not append attendance");
    require_int(attendance_app_admin_is_active() == 1u, "admin card should enter admin mode");
    require_int(strcmp(capture.text, "ADMIN:ON\n") == 0,
                "admin card should report admin entry");

    capture.text[0] = '\0';
    g_card_type = ATT_CARD_NORMAL;
    g_card_uid.bytes[0] = 0x10;
    g_card_uid.bytes[1] = 0x20;
    g_card_uid.bytes[2] = 0x30;
    g_card_uid.bytes[3] = 0x40;
    g_now_sec = 101u;
    attendance_app_poll_nfc();

    require_int(g_append_calls == 0u, "normal card in admin mode should not append");
    require_int(attendance_app_admin_is_active() == 1u, "denied normal card should keep admin mode active");
    require_int(strcmp(capture.text, "ADMIN:ERR:CARD_DENIED\n") == 0,
                "normal card in admin mode should be rejected");
}

static void test_admin_actions_save_device_and_mode(void)
{
    reset_mocks();
    g_card_type = ATT_CARD_ADMIN;
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';

    require_int(attendance_app_admin_handle_action(ATT_ADMIN_ACTION_INC) == ATT_ADMIN_RESULT_HANDLED,
                "admin increment should be handled");
    require_int(attendance_app_admin_handle_action(ATT_ADMIN_ACTION_NEXT_FIELD) == ATT_ADMIN_RESULT_HANDLED,
                "admin field switch should be handled");
    require_int(attendance_app_admin_handle_action(ATT_ADMIN_ACTION_DEC) == ATT_ADMIN_RESULT_HANDLED,
                "admin mode decrement should be handled");
    require_int(attendance_app_admin_handle_action(ATT_ADMIN_ACTION_SAVE) == ATT_ADMIN_RESULT_SAVED,
                "admin save should request reset");

    require_int(g_config.device_id == 43u, "admin save should persist changed device id");
    require_int(g_config.work_mode == ATT_MODE_CHECK_OUT, "admin save should persist changed mode");
    require_int(attendance_app_admin_is_active() == 0u, "admin mode should leave after save");
    require_int(strcmp(capture.text, "ADMIN:SAVED\n") == 0,
                "admin save should report success");
}

static void test_admin_mode_times_out_after_120_seconds(void)
{
    reset_mocks();
    g_card_type = ATT_CARD_ADMIN;
    send_capture_t capture = {0};
    init_app(&capture);

    attendance_app_poll_nfc();
    capture.text[0] = '\0';

    g_card_status = ATT_ERR_NO_CARD;
    g_now_sec = 220u;
    attendance_app_poll_nfc();

    require_int(attendance_app_admin_is_active() == 0u, "admin mode should timeout after 120 seconds");
    require_int(strcmp(capture.text, "ADMIN:TIMEOUT\n") == 0,
                "admin timeout should report timeout");
}

int main(void)
{
    test_records_first_card();
    test_skips_same_uid_inside_repeat_interval();
    test_records_same_uid_after_repeat_interval();
    test_lru_cache_avoids_flash_scan_for_recent_uid();
    test_next_seq_uses_latest_stored_record_after_init();
    test_in_out_mode_alternates_back_to_entry();
    test_out_duration_supports_cross_day();
    test_check_in_mode_rejects_already_inside_card();
    test_check_out_mode_rejects_card_without_entry();
    test_no_card_is_silent();
    test_invalid_card_is_reported_without_append();
    test_storage_error_is_reported_without_consuming_seq();
    test_cardlock_pauses_nfc_poll();
    test_admin_card_enters_mode_and_denies_normal_card();
    test_admin_actions_save_device_and_mode();
    test_admin_mode_times_out_after_120_seconds();
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
    person->card_type = g_card_type;
    if (g_read_person_status != ATT_OK) {
        return g_read_person_status;
    }
    return g_card_status;
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
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_config = *config;
    return ATT_OK;
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
    g_append_calls++;
    if (g_append_status != ATT_OK) {
        return g_append_status;
    }
    if (g_record_count >= (sizeof(g_records) / sizeof(g_records[0]))) {
        return ATT_ERR_STORAGE;
    }
    g_last_record = *record;
    g_records[g_record_count++] = *record;
    return ATT_OK;
}

att_status_t att_storage_read_record(uint32_t index, att_record_t *record)
{
    g_read_record_calls++;
    if (record == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (index >= g_record_count) {
        return ATT_ERR_STORAGE;
    }
    *record = g_records[index];
    return ATT_OK;
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
