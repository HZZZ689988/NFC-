#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_card.h"
#include "att_protocol.h"
#include "att_storage.h"

typedef struct {
    char text[512];
} send_capture_t;

static att_uid_t g_card_uid = {{0xA1, 0xB2, 0xC3, 0xD4}};
static att_status_t g_read_uid_status = ATT_OK;
static att_status_t g_diag_status = ATT_OK;
static att_card_diag_t g_diag;
static att_status_t g_issue_status = ATT_OK;
static att_status_t g_clear_status = ATT_OK;
static att_person_t g_last_person;
static att_uid_t g_last_clear_uid;
static att_record_t g_records[3];
static uint32_t g_record_count;
static unsigned g_issue_calls;
static unsigned g_clear_calls;
static att_card_image_area_t g_last_image_area;
static uint8_t g_last_image_index;
static uint8_t g_last_image_block[16];
static unsigned g_image_calls;
static unsigned g_update_image_calls;
static unsigned g_oled_test_calls;
static att_device_config_t g_config;
static unsigned g_save_config_calls;
static unsigned g_apply_config_calls;
static att_device_config_t g_saved_config;
static att_device_config_t g_applied_config;

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

static void reset_mocks(void)
{
    memset(&g_last_person, 0, sizeof(g_last_person));
    memset(&g_last_clear_uid, 0, sizeof(g_last_clear_uid));
    g_card_uid.bytes[0] = 0xA1;
    g_card_uid.bytes[1] = 0xB2;
    g_card_uid.bytes[2] = 0xC3;
    g_card_uid.bytes[3] = 0xD4;
    g_read_uid_status = ATT_OK;
    g_diag_status = ATT_OK;
    memset(&g_diag, 0, sizeof(g_diag));
    g_diag.version_raw = 0x92u;
    g_diag.version = 0x92u;
    g_diag.command = 0x00u;
    g_diag.com_irq = 0x01u;
    g_diag.fifo_level = 0x00u;
    g_diag.tx_control = 0x03u;
    g_diag.error = 0x00u;
    g_diag.pins = 0x3Du;
    g_diag.shared_mosi_flash_cs = 1u;
    g_diag.serial_speed_before = 0x00u;
    g_diag.serial_speed_test = 0x7Au;
    g_diag.serial_speed_after = 0x00u;
    g_diag.spi_rw_ok = 1u;
    g_diag.request_status = -2;
    g_diag.tag_type[0] = 0x04u;
    g_diag.tag_type[1] = 0x00u;
    g_issue_status = ATT_OK;
    g_clear_status = ATT_OK;
    memset(g_records, 0, sizeof(g_records));
    g_record_count = 0u;
    g_issue_calls = 0u;
    g_clear_calls = 0u;
    g_last_image_area = ATT_CARD_IMAGE_PORTRAIT;
    g_last_image_index = 0u;
    memset(g_last_image_block, 0, sizeof(g_last_image_block));
    g_image_calls = 0u;
    g_update_image_calls = 0u;
    g_oled_test_calls = 0u;
    memset(&g_config, 0, sizeof(g_config));
    g_config.device_id = 1u;
    g_config.work_mode = ATT_MODE_IN_OUT;
    g_config.upload_enable = 1u;
    g_config.repeat_interval_sec = 60u;
    g_config.server_port = 9000u;
    g_config.timezone = 8;
    snprintf(g_config.server_host, sizeof(g_config.server_host), "%s", "192.168.1.10");
    g_save_config_calls = 0u;
    g_apply_config_calls = 0u;
    memset(&g_saved_config, 0, sizeof(g_saved_config));
    memset(&g_applied_config, 0, sizeof(g_applied_config));
    att_protocol_set_config_apply(NULL, NULL);
}

static void test_read_returns_uid(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("READ", capture_send, &capture);

    require_int(status == ATT_OK, "READ should return ATT_OK");
    require_int(strcmp(capture.text, "UID:A1B2C3D4\n") == 0, "READ should return UID line");
}

static void test_read_maps_no_card(void)
{
    reset_mocks();
    g_read_uid_status = ATT_ERR_NO_CARD;
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("READ", capture_send, &capture);

    require_int(status == ATT_ERR_NO_CARD, "READ should return no-card status");
    require_int(strcmp(capture.text, "ERR:NO_CARD\n") == 0, "READ should map no card");
}

static void test_issue_calls_card_writer(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("ISSUE:A1B2C3D4,1001,7,1", capture_send, &capture);

    require_int(status == ATT_OK, "ISSUE should return ATT_OK");
    require_int(strcmp(capture.text, "OK:ISSUE\n") == 0, "ISSUE should acknowledge write");
    require_int(g_issue_calls == 1u, "ISSUE should call card writer");
    require_int(g_last_person.uid.bytes[0] == 0xA1 && g_last_person.uid.bytes[3] == 0xD4,
                "ISSUE should parse UID");
    require_int(g_last_person.sid == 1001u, "ISSUE should parse SID");
    require_int(g_last_person.points == 7u, "ISSUE should parse points");
    require_int(g_last_person.card_type == ATT_CARD_IMAGE, "ISSUE should parse card type");
}

static void test_issue_maps_uid_mismatch(void)
{
    reset_mocks();
    g_issue_status = ATT_ERR_CID_MISMATCH;
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("ISSUE:A1B2C3D4,1001,0,0", capture_send, &capture);

    require_int(status == ATT_ERR_CID_MISMATCH, "ISSUE should return mismatch status");
    require_int(strcmp(capture.text, "ERR:UID_MISMATCH\n") == 0, "ISSUE should map mismatch");
}

static void test_clear_calls_card_clear(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("CLEAR:A1B2C3D4", capture_send, &capture);

    require_int(status == ATT_OK, "CLEAR should return ATT_OK");
    require_int(strcmp(capture.text, "OK:CLEAR\n") == 0, "CLEAR should acknowledge clear");
    require_int(g_clear_calls == 1u, "CLEAR should call card clear");
    require_int(g_last_clear_uid.bytes[0] == 0xA1 && g_last_clear_uid.bytes[3] == 0xD4,
                "CLEAR should parse UID");
}

static void test_list_streams_records(void)
{
    reset_mocks();
    g_record_count = 2u;
    g_records[0].seq = 1u;
    g_records[0].uid.bytes[0] = 0xA1;
    g_records[0].uid.bytes[1] = 0xB2;
    g_records[0].uid.bytes[2] = 0xC3;
    g_records[0].uid.bytes[3] = 0xD4;
    g_records[0].sid = 1001u;
    g_records[0].type = ATT_RECORD_IN;
    g_records[0].timestamp = 1782691200u;
    g_records[0].device_id = 7u;
    g_records[1] = g_records[0];
    g_records[1].seq = 2u;
    g_records[1].sid = 1002u;
    g_records[1].type = ATT_RECORD_OUT;

    send_capture_t capture = {0};
    att_status_t status = att_protocol_handle_line("LIST:1", capture_send, &capture);

    require_int(status == ATT_OK, "LIST should return ATT_OK");
    require_int(strcmp(capture.text,
                       "LIST:COUNT=2\n"
                       "REC:SEQ=2|UID=A1B2C3D4|SID=1002|OUT|1782691200|DEV=7|OK\n"
                       "LIST:END\n") == 0,
                "LIST should stream newest requested record");
}

static void test_list_rejects_bad_count_before_storage_access(void)
{
    reset_mocks();
    g_record_count = 2u;
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("LIST:BAD", capture_send, &capture);

    require_int(status == ATT_ERR_INVALID_ARG, "bad LIST count should return invalid arg");
    require_int(strcmp(capture.text, "ERR:ARG\n") == 0,
                "bad LIST count should only report argument error");
}

static att_status_t capture_config_apply(const att_device_config_t *config, void *ctx)
{
    (void)ctx;
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_applied_config = *config;
    g_apply_config_calls++;
    return ATT_OK;
}

static void test_config_query_returns_persisted_config(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("CFG?", capture_send, &capture);

    require_int(status == ATT_OK, "CFG? should return ATT_OK");
    require_int(strcmp(capture.text,
                       "CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.1.10|PORT=9000|TZ=8|SSID=|WLOC=\n") == 0,
                "CFG? should return stored config fields");
}

static void test_diag_query_returns_rc522_registers(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("DIAG?", capture_send, &capture);

    require_int(status == ATT_OK, "DIAG? should return ATT_OK");
    require_int(strcmp(capture.text,
                       "DIAG:RC522_RAW=0x92|RC522_VER=0x92|CMD=0x00|IRQ=0x01|FIFO=0x00|TX=0x03|ERR=0x00|PINS=0x3D|SHARE=1|SPD=0x00->0x7A->0x00|RW=1|REQ=-2|TAG=0400\n") == 0,
                "DIAG? should return RC522 diagnostic fields");
}

static void test_diag_query_maps_driver_error(void)
{
    reset_mocks();
    g_diag_status = ATT_ERR_NOT_READY;
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("DIAG?", capture_send, &capture);

    require_int(status == ATT_ERR_NOT_READY, "DIAG? should return card diagnostic status");
    require_int(strcmp(capture.text, "ERR:DIAG\n") == 0, "DIAG? should map diagnostic error");
}

static void test_oled_test_command_triggers_display(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("OLEDTEST", capture_send, &capture);

    require_int(status == ATT_OK, "OLEDTEST should return ATT_OK");
    require_int(strcmp(capture.text, "OK:OLEDTEST\n") == 0, "OLEDTEST should acknowledge command");
    require_int(g_oled_test_calls == 1u, "OLEDTEST should trigger display test");
}

static void test_config_set_saves_and_applies_config(void)
{
    reset_mocks();
    att_protocol_set_config_apply(capture_config_apply, NULL);
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line(
        "CFG:DEV=7|MODE=2|UPLOAD=0|REPEAT=120|TZ=8", capture_send, &capture);

    require_int(status == ATT_OK, "CFG set should return ATT_OK");
    require_int(strcmp(capture.text, "OK:CFG\n") == 0, "CFG set should acknowledge update");
    require_int(g_save_config_calls == 1u, "CFG set should save config");
    require_int(g_apply_config_calls == 1u, "CFG set should apply runtime config");
    require_int(g_saved_config.device_id == 7u, "CFG set should update device id");
    require_int(g_saved_config.work_mode == ATT_MODE_CHECK_OUT, "CFG set should update mode");
    require_int(g_saved_config.upload_enable == 0u, "CFG set should update upload flag");
    require_int(g_saved_config.repeat_interval_sec == 120u, "CFG set should update repeat interval");
    require_int(g_applied_config.device_id == 7u, "CFG set should pass saved config to callback");
}

static void test_config_set_rejects_bad_value(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("CFG:PORT=0", capture_send, &capture);

    require_int(status == ATT_ERR_INVALID_ARG, "bad CFG value should return invalid arg");
    require_int(strcmp(capture.text, "ERR:ARG\n") == 0, "bad CFG value should report argument error");
    require_int(g_save_config_calls == 0u, "bad CFG value should not save config");
}

static void test_image_block_command_calls_card_writer(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("IMGN09:00112233445566778899AABBCCDDEEFF", capture_send, &capture);

    require_int(status == ATT_OK, "image block should return ATT_OK");
    require_int(strcmp(capture.text, "OK:IMG\n") == 0, "image block should acknowledge write");
    require_int(g_image_calls == 1u, "image block should call card writer");
    require_int(g_last_image_area == ATT_CARD_IMAGE_NAME, "image block should parse area");
    require_int(g_last_image_index == 9u, "image block should parse index");
    require_int(g_last_image_block[0] == 0x00 && g_last_image_block[15] == 0xFF,
                "image block should parse hex payload");
}

static void test_update_image_calls_card_finish(void)
{
    reset_mocks();
    send_capture_t capture = {0};

    att_status_t status = att_protocol_handle_line("UPDATEIMG", capture_send, &capture);

    require_int(status == ATT_OK, "UPDATEIMG should return ATT_OK");
    require_int(strcmp(capture.text, "OK:UPDATEIMG\n") == 0, "UPDATEIMG should acknowledge update");
    require_int(g_update_image_calls == 1u, "UPDATEIMG should call card finish");
}

int main(void)
{
    test_read_returns_uid();
    test_read_maps_no_card();
    test_issue_calls_card_writer();
    test_issue_maps_uid_mismatch();
    test_clear_calls_card_clear();
    test_list_streams_records();
    test_list_rejects_bad_count_before_storage_access();
    test_config_query_returns_persisted_config();
    test_diag_query_returns_rc522_registers();
    test_diag_query_maps_driver_error();
    test_oled_test_command_triggers_display();
    test_config_set_saves_and_applies_config();
    test_config_set_rejects_bad_value();
    test_image_block_command_calls_card_writer();
    test_update_image_calls_card_finish();
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
    return g_read_uid_status;
}

att_status_t att_card_read_person(att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    memset(person, 0, sizeof(*person));
    person->uid = g_card_uid;
    person->sid = 1001u;
    person->card_type = ATT_CARD_NORMAL;
    return g_read_uid_status;
}

att_status_t att_card_diag(att_card_diag_t *diag)
{
    if (diag == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    *diag = g_diag;
    return g_diag_status;
}

att_status_t att_card_issue_checked(const att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_last_person = *person;
    g_issue_calls++;
    return g_issue_status;
}

att_status_t att_card_clear_checked(const att_uid_t *expected_uid)
{
    if (expected_uid == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_last_clear_uid = *expected_uid;
    g_clear_calls++;
    return g_clear_status;
}

att_status_t att_card_write_image_block(att_card_image_area_t area, uint8_t index, const uint8_t data[16])
{
    if (data == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    g_last_image_area = area;
    g_last_image_index = index;
    memcpy(g_last_image_block, data, sizeof(g_last_image_block));
    g_image_calls++;
    return ATT_OK;
}

att_status_t att_card_finish_image_update(void)
{
    g_update_image_calls++;
    return ATT_OK;
}

void att_display_show_oled_test(void)
{
    g_oled_test_calls++;
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
    g_saved_config = *config;
    g_config = *config;
    g_save_config_calls++;
    return ATT_OK;
}

void att_storage_default_config(att_device_config_t *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
    }
}

att_status_t att_storage_append_record(const att_record_t *record)
{
    return record == NULL ? ATT_ERR_INVALID_ARG : ATT_OK;
}

att_status_t att_storage_read_record(uint32_t index, att_record_t *record)
{
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
