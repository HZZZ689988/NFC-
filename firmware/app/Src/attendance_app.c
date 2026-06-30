#include "attendance_app.h"

#include <stdio.h>
#include <string.h>

#include "att_card.h"
#include "att_display.h"
#include "att_protocol.h"
#include "att_storage.h"

#ifndef ATT_ENABLE_NETWORK
#define ATT_ENABLE_NETWORK 0
#endif

#if ATT_ENABLE_NETWORK
#include "att_network.h"
#endif

#define ATT_NETWORK_UPLOAD_INTERVAL_SEC     10u
#define ATT_NETWORK_HEARTBEAT_INTERVAL_SEC  60u
#define ATT_NETWORK_TIME_SYNC_INTERVAL_SEC  3600u
#define ATT_NETWORK_WEATHER_INTERVAL_SEC    1800u
#define ATT_WEATHER_TEXT_LEN                32u

static att_device_config_t s_config;
static uint32_t s_next_seq = 1u;
static att_protocol_send_fn s_serial_send;
static void *s_serial_send_ctx;
static attendance_app_time_fn s_time_now;
static void *s_time_ctx;
static attendance_feedback_fn s_feedback;
static void *s_feedback_ctx;
static char s_serial_line[ATT_SERIAL_LINE_MAX + 1u];
static size_t s_serial_line_len;
static uint8_t s_serial_line_overflow;
static att_uid_t s_last_uid;
static uint32_t s_last_uid_time;
static uint8_t s_last_uid_valid;
static uint32_t s_last_network_upload_time;
static uint32_t s_last_network_heartbeat_time;
static uint32_t s_last_network_time_sync_time;
static uint32_t s_last_weather_time;
static uint8_t s_network_upload_due;
static uint8_t s_network_heartbeat_due;
static uint8_t s_network_time_sync_due;
static uint8_t s_weather_due;
static uint8_t s_network_ready;
static att_display_network_state_t s_network_display_state;

static void default_serial_send(const char *line, void *ctx)
{
    (void)line;
    (void)ctx;
}

static void default_feedback(attendance_feedback_event_t event, void *ctx)
{
    (void)event;
    (void)ctx;
}

static void emit_feedback(attendance_feedback_event_t event)
{
    if (s_feedback == NULL) {
        s_feedback = default_feedback;
        s_feedback_ctx = NULL;
    }
    s_feedback(event, s_feedback_ctx);
}

static void set_network_state(att_display_network_state_t state)
{
    if (state != s_network_display_state) {
        if (state == ATT_DISPLAY_NET_ERROR) {
            emit_feedback(ATT_FEEDBACK_NETWORK_FAULT);
        } else if (state == ATT_DISPLAY_NET_ONLINE || state == ATT_DISPLAY_NET_UPLOAD) {
            emit_feedback(ATT_FEEDBACK_NETWORK_ONLINE);
        }
        s_network_display_state = state;
    }

    att_display_set_network(state);
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

static uint32_t app_now(void)
{
    if (s_time_now == NULL) {
        s_time_now = default_time_now;
        s_time_ctx = NULL;
    }

    return s_time_now(s_time_ctx);
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

    s_serial_send = default_serial_send;
    s_serial_send_ctx = NULL;
    s_time_now = default_time_now;
    s_time_ctx = NULL;
    s_feedback = default_feedback;
    s_feedback_ctx = NULL;
    s_network_display_state = ATT_DISPLAY_NET_OFF;

    att_display_set_config(&s_config);
    att_display_set_record_count(count);
    char weather[ATT_WEATHER_TEXT_LEN];
    if (att_storage_load_weather(weather, sizeof(weather)) == ATT_OK && weather[0] != '\0') {
        att_display_set_weather(weather);
    }

    (void)att_card_init();
#if ATT_ENABLE_NETWORK
    s_network_ready = (att_network_init(&s_config) == ATT_OK) ? 1u : 0u;
#else
    s_network_ready = 0u;
#endif
    set_network_state(s_network_ready ? ATT_DISPLAY_NET_READY : ATT_DISPLAY_NET_OFF);
    s_serial_line_len = 0u;
    s_serial_line_overflow = 0u;
    s_last_uid_valid = 0u;
    s_last_uid_time = 0u;
    s_last_network_upload_time = 0u;
    s_last_network_heartbeat_time = 0u;
    s_last_network_time_sync_time = 0u;
    s_last_weather_time = 0u;
    s_network_upload_due = 1u;
    s_network_heartbeat_due = 1u;
    s_network_time_sync_due = 1u;
    s_weather_due = 1u;
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

void attendance_app_set_feedback(attendance_feedback_fn feedback, void *ctx)
{
    s_feedback = (feedback != NULL) ? feedback : default_feedback;
    s_feedback_ctx = ctx;
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
    att_person_t person;
    att_status_t status = att_card_read_person(&person);
    if (status == ATT_ERR_NO_CARD) {
        return;
    }
    if (status == ATT_ERR_CRC || status == ATT_ERR_CID_MISMATCH) {
        send_line("ATTEND:ERR:INVALID_CARD\n");
        emit_feedback(ATT_FEEDBACK_CARD_INVALID);
        att_display_show_attendance_invalid(app_now());
        return;
    }
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:CARD\n");
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_error("CARD READ", app_now());
        return;
    }

    uint32_t now = app_now();
    if (is_duplicate_uid(&person.uid, now)) {
        send_line("ATTEND:SKIP:DUPLICATE\n");
        emit_feedback(ATT_FEEDBACK_ATTEND_DUPLICATE);
        att_display_show_attendance_duplicate(now);
        return;
    }

    att_record_t record;
    memset(&record, 0, sizeof(record));
    record.seq = s_next_seq;
    record.uid = person.uid;
    record.sid = person.sid;
    record.type = ATT_RECORD_NORMAL;
    record.timestamp = now;
    record.device_id = s_config.device_id;
    record.upload_state = ATT_UPLOAD_PENDING;

    status = att_storage_append_record(&record);
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:STORAGE\n");
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_error("STORAGE", now);
        return;
    }

    s_last_uid = person.uid;
    s_last_uid_time = now;
    s_last_uid_valid = 1u;
    s_next_seq++;

    char line[32];
    snprintf(line, sizeof(line), "ATTEND:OK:SEQ=%lu\n", (unsigned long)record.seq);
    send_line(line);
    emit_feedback(ATT_FEEDBACK_ATTEND_OK);
    att_display_show_attendance_ok(record.seq, record.sid, now);
}

void attendance_app_poll_serial(void)
{
    /* Serial bytes are dispatched by the STM32 UartDrv receive queue. */
}

void attendance_app_poll_network(void)
{
#if ATT_ENABLE_NETWORK
    if (!s_config.upload_enable) {
        set_network_state(ATT_DISPLAY_NET_OFF);
        return;
    }

    if (s_network_ready == 0u) {
        set_network_state(ATT_DISPLAY_NET_ERROR);
        return;
    }

    uint32_t now = app_now();
    if (s_network_time_sync_due ||
        (uint32_t)(now - s_last_network_time_sync_time) >= ATT_NETWORK_TIME_SYNC_INTERVAL_SEC) {
        att_status_t time_status = att_network_sync_time();
        s_last_network_time_sync_time = now;
        s_network_time_sync_due = 0u;
        set_network_state(time_status == ATT_OK ?
                                ATT_DISPLAY_NET_ONLINE :
                                ATT_DISPLAY_NET_ERROR);
    }

    if (s_weather_due ||
        (uint32_t)(now - s_last_weather_time) >= ATT_NETWORK_WEATHER_INTERVAL_SEC) {
        char weather[ATT_WEATHER_TEXT_LEN];
        att_status_t weather_status = att_network_query_weather(weather, sizeof(weather));
        s_last_weather_time = now;
        s_weather_due = 0u;
        if (weather_status == ATT_OK && weather[0] != '\0') {
            att_display_set_weather(weather);
            (void)att_storage_save_weather(weather);
            set_network_state(ATT_DISPLAY_NET_ONLINE);
        } else if (weather_status == ATT_ERR_NOT_READY) {
            set_network_state(ATT_DISPLAY_NET_ONLINE);
        } else {
            set_network_state(ATT_DISPLAY_NET_ERROR);
        }
    }
    if (s_network_heartbeat_due ||
        (uint32_t)(now - s_last_network_heartbeat_time) >= ATT_NETWORK_HEARTBEAT_INTERVAL_SEC) {
        (void)att_network_send_heartbeat();
        s_last_network_heartbeat_time = now;
        s_network_heartbeat_due = 0u;
        set_network_state(ATT_DISPLAY_NET_ONLINE);
    }

    if (s_network_upload_due ||
        (uint32_t)(now - s_last_network_upload_time) >= ATT_NETWORK_UPLOAD_INTERVAL_SEC) {
        att_status_t upload_status = att_network_upload_pending();
        s_last_network_upload_time = now;
        s_network_upload_due = 0u;
        set_network_state(upload_status == ATT_OK ?
                                ATT_DISPLAY_NET_UPLOAD :
                                ATT_DISPLAY_NET_ONLINE);
    }
#endif
}

void attendance_app_mark_network_ready(void)
{
#if ATT_ENABLE_NETWORK
    s_network_ready = 1u;
    s_network_upload_due = 1u;
    s_network_heartbeat_due = 1u;
    s_network_time_sync_due = 1u;
    s_weather_due = 1u;
    set_network_state(ATT_DISPLAY_NET_ONLINE);
#endif
}
