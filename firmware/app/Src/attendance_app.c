#include "attendance_app.h"

#include <stdio.h>
#include <string.h>

#include "att_card.h"
#include "att_protocol.h"
#include "att_storage.h"

#ifndef ATT_ENABLE_NETWORK
#define ATT_ENABLE_NETWORK 0
#endif

#if ATT_ENABLE_NETWORK
#include "att_network.h"
#endif

static att_device_config_t s_config;
static uint32_t s_next_seq = 1u;
static att_protocol_send_fn s_serial_send;
static void *s_serial_send_ctx;
static attendance_app_time_fn s_time_now;
static void *s_time_ctx;
static char s_serial_line[ATT_SERIAL_LINE_MAX + 1u];
static size_t s_serial_line_len;
static uint8_t s_serial_line_overflow;
static att_uid_t s_last_uid;
static uint32_t s_last_uid_time;
static uint8_t s_last_uid_valid;

static void default_serial_send(const char *line, void *ctx)
{
    (void)line;
    (void)ctx;
}

static void dispatch_serial_line(void)
{
    s_serial_line[s_serial_line_len] = '\0';
    (void)att_protocol_handle_line(s_serial_line, s_serial_send, s_serial_send_ctx);
    s_serial_line_len = 0u;
}

static uint32_t default_time_now(void *ctx)
{
    (void)ctx;
    return 0u;
}

static uint8_t uid_equal(const att_uid_t *left, const att_uid_t *right)
{
    return (uint8_t)(memcmp(left->bytes, right->bytes, ATT_UID_LEN) == 0);
}

static void send_line(const char *line)
{
    if (s_serial_send == NULL) {
        s_serial_send = default_serial_send;
        s_serial_send_ctx = NULL;
    }
    s_serial_send(line, s_serial_send_ctx);
}

static uint8_t is_duplicate_uid(const att_uid_t *uid, uint32_t now)
{
    if (s_last_uid_valid == 0u || !uid_equal(uid, &s_last_uid)) {
        return 0u;
    }

    if (s_config.repeat_interval_sec == 0u) {
        return 0u;
    }

    return (uint8_t)((uint32_t)(now - s_last_uid_time) < (uint32_t)s_config.repeat_interval_sec);
}

att_status_t attendance_app_init(void)
{
    att_status_t status = att_storage_init();
    if (status != ATT_OK) {
        return status;
    }

    status = att_storage_load_config(&s_config);
    if (status != ATT_OK) {
        att_storage_default_config(&s_config);
        (void)att_storage_save_config(&s_config);
    }

    uint32_t count = 0u;
    if (att_storage_record_count(&count) == ATT_OK) {
        s_next_seq = count + 1u;
    }

    (void)att_card_init();
#if ATT_ENABLE_NETWORK
    (void)att_network_init(&s_config);
#endif
    s_serial_send = default_serial_send;
    s_serial_send_ctx = NULL;
    s_time_now = default_time_now;
    s_time_ctx = NULL;
    s_serial_line_len = 0u;
    s_serial_line_overflow = 0u;
    s_last_uid_valid = 0u;
    s_last_uid_time = 0u;
    memset(&s_last_uid, 0, sizeof(s_last_uid));
    return ATT_OK;
}

void attendance_app_set_serial_send(att_protocol_send_fn send, void *ctx)
{
    s_serial_send = (send != NULL) ? send : default_serial_send;
    s_serial_send_ctx = ctx;
}

void attendance_app_set_time_source(attendance_app_time_fn now, void *ctx)
{
    s_time_now = (now != NULL) ? now : default_time_now;
    s_time_ctx = ctx;
}

void attendance_app_dispatch_serial_bytes(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return;
    }

    if (s_serial_send == NULL) {
        s_serial_send = default_serial_send;
        s_serial_send_ctx = NULL;
    }

    for (size_t i = 0u; i < len; ++i) {
        uint8_t ch = data[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (s_serial_line_overflow != 0u) {
                s_serial_send("ERR:ARG\n", s_serial_send_ctx);
            } else if (s_serial_line_len > 0u) {
                dispatch_serial_line();
            }

            s_serial_line_len = 0u;
            s_serial_line_overflow = 0u;
            continue;
        }

        if (s_serial_line_overflow != 0u) {
            continue;
        }

        if (s_serial_line_len >= ATT_SERIAL_LINE_MAX) {
            s_serial_line_overflow = 1u;
            s_serial_line_len = 0u;
            continue;
        }

        s_serial_line[s_serial_line_len++] = (char)ch;
    }
}

void attendance_app_poll_nfc(void)
{
    att_uid_t uid;
    att_status_t status = att_card_read_uid(&uid);
    if (status == ATT_ERR_NO_CARD) {
        return;
    }
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:CARD\n");
        return;
    }

    if (s_time_now == NULL) {
        s_time_now = default_time_now;
        s_time_ctx = NULL;
    }

    uint32_t now = s_time_now(s_time_ctx);
    if (is_duplicate_uid(&uid, now)) {
        send_line("ATTEND:SKIP:DUPLICATE\n");
        return;
    }

    att_record_t record;
    memset(&record, 0, sizeof(record));
    record.seq = s_next_seq;
    record.uid = uid;
    record.type = ATT_RECORD_NORMAL;
    record.timestamp = now;
    record.device_id = s_config.device_id;
    record.upload_state = ATT_UPLOAD_PENDING;

    status = att_storage_append_record(&record);
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:STORAGE\n");
        return;
    }

    s_last_uid = uid;
    s_last_uid_time = now;
    s_last_uid_valid = 1u;
    s_next_seq++;

    char line[32];
    snprintf(line, sizeof(line), "ATTEND:OK:SEQ=%lu\n", (unsigned long)record.seq);
    send_line(line);
}

void attendance_app_poll_serial(void)
{
    /* Serial bytes are dispatched by the STM32 UartDrv receive queue. */
}

void attendance_app_poll_network(void)
{
#if ATT_ENABLE_NETWORK
    (void)att_network_upload_pending();
#endif
}
