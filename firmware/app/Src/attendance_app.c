#include "attendance_app.h"

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
static char s_serial_line[ATT_SERIAL_LINE_MAX + 1u];
static size_t s_serial_line_len;
static uint8_t s_serial_line_overflow;

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
    s_serial_line_len = 0u;
    s_serial_line_overflow = 0u;
    return ATT_OK;
}

void attendance_app_set_serial_send(att_protocol_send_fn send, void *ctx)
{
    s_serial_send = (send != NULL) ? send : default_serial_send;
    s_serial_send_ctx = ctx;
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
    if (att_card_read_uid(&uid) != ATT_OK) {
        return;
    }

    att_record_t record;
    memset(&record, 0, sizeof(record));
    record.seq = s_next_seq++;
    record.uid = uid;
    record.type = ATT_RECORD_NORMAL;
    record.device_id = s_config.device_id;
    record.upload_state = ATT_UPLOAD_PENDING;

    (void)att_storage_append_record(&record);
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
