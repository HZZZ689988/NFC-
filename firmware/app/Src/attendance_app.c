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

static uint32_t app_now(void);

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

static int hex_to_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static uint8_t parse_uid_hex(const char *text, att_uid_t *uid)
{
    if (text == NULL || uid == NULL) {
        return 0u;
    }

    for (size_t i = 0u; i < ATT_UID_LEN; ++i) {
        int hi = hex_to_nibble(text[i * 2u]);
        int lo = hex_to_nibble(text[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return 0u;
        }
        uid->bytes[i] = (uint8_t)((hi << 4) | lo);
    }

    return 1u;
}

static uint8_t parse_u32_until(const char *text, char terminator, uint32_t max_value,
                               uint32_t *value, const char **end)
{
    if (text == NULL || value == NULL || end == NULL || *text == '\0') {
        return 0u;
    }

    uint32_t parsed = 0u;
    const char *p = text;
    while (*p != '\0' && *p != terminator) {
        if (*p < '0' || *p > '9') {
            return 0u;
        }

        uint32_t digit = (uint32_t)(*p - '0');
        if (parsed > (UINT32_MAX - digit) / 10u) {
            return 0u;
        }
        parsed = (parsed * 10u) + digit;
        if (parsed > max_value) {
            return 0u;
        }
        p++;
    }

    if (p == text) {
        return 0u;
    }

    *value = parsed;
    *end = p;
    return 1u;
}

static att_status_t append_attendance_record(const att_uid_t *uid, uint32_t sid,
                                             att_record_type_t type, uint32_t now,
                                             uint32_t *seq_out)
{
    if (uid == NULL || seq_out == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    att_record_t record;
    memset(&record, 0, sizeof(record));
    record.seq = s_next_seq;
    record.uid = *uid;
    record.sid = sid;
    record.type = type;
    record.timestamp = now;
    record.device_id = s_config.device_id;
    record.upload_state = ATT_UPLOAD_PENDING;

    att_status_t status = att_storage_append_record(&record);
    if (status != ATT_OK) {
        return status;
    }

    *seq_out = record.seq;
    s_next_seq++;
    s_network_upload_due = 1u;
    att_display_set_record_count(record.seq);
    return ATT_OK;
}

static att_status_t handle_sim_attendance(const char *payload, uint32_t *seq_out,
                                          uint32_t *sid_out)
{
    static const char prefix[] = "SIMATT:";
    if (payload == NULL || seq_out == NULL || sid_out == NULL ||
        strncmp(payload, prefix, sizeof(prefix) - 1u) != 0) {
        return ATT_ERR_INVALID_ARG;
    }

    const char *cursor = payload + sizeof(prefix) - 1u;
    att_uid_t uid;
    if (parse_uid_hex(cursor, &uid) == 0u || cursor[ATT_UID_HEX_LEN] != ',') {
        return ATT_ERR_INVALID_ARG;
    }
    cursor += ATT_UID_HEX_LEN + 1u;

    uint32_t sid = 0u;
    const char *end = NULL;
    if (parse_u32_until(cursor, ',', UINT32_MAX, &sid, &end) == 0u ||
        *end != ',' || sid == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    cursor = end + 1u;

    uint32_t type_value = 0u;
    if (parse_u32_until(cursor, '\0', ATT_RECORD_NORMAL, &type_value, &end) == 0u ||
        *end != '\0') {
        return ATT_ERR_INVALID_ARG;
    }

    uint32_t now = app_now();
    att_status_t status = append_attendance_record(&uid, sid,
                                                   (att_record_type_t)type_value,
                                                   now, seq_out);
    if (status != ATT_OK) {
        return status;
    }

    *sid_out = sid;
    emit_feedback(ATT_FEEDBACK_ATTEND_OK);
    att_display_show_attendance_ok(*seq_out, sid, now);
    return ATT_OK;
}
static att_status_t apply_runtime_config(const att_device_config_t *config, void *ctx)
{
    (void)ctx;
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    s_config = *config;
    att_display_set_config(&s_config);
#if ATT_ENABLE_NETWORK
    s_network_ready = (att_network_init(&s_config) == ATT_OK) ? 1u : 0u;
    s_network_upload_due = 1u;
    s_network_heartbeat_due = 1u;
    s_network_time_sync_due = 1u;
    s_weather_due = 1u;
    set_network_state(s_network_ready ? ATT_DISPLAY_NET_READY : ATT_DISPLAY_NET_ERROR);
#else
    s_network_ready = 0u;
    set_network_state(ATT_DISPLAY_NET_OFF);
#endif
    return ATT_OK;
}

static void dispatch_serial_line(void)
{
    s_serial_line[s_serial_line_len] = '\0';

    char payload[ATT_SERIAL_LINE_MAX + 1u];
    if (att_protocol_parse_frame(s_serial_line, payload, sizeof(payload)) == ATT_OK &&
        strncmp(payload, "SIMATT:", 7) == 0) {
        uint32_t seq = 0u;
        uint32_t sid = 0u;
        att_status_t status = handle_sim_attendance(payload, &seq, &sid);
        if (status == ATT_OK) {
            char response[32];
            snprintf(response, sizeof(response), "OK:SIMATT:SEQ=%lu\n", (unsigned long)seq);
            s_serial_send(response, s_serial_send_ctx);
        } else if (status == ATT_ERR_STORAGE) {
            s_serial_send("ERR:STORAGE\n", s_serial_send_ctx);
        } else {
            s_serial_send("ERR:ARG\n", s_serial_send_ctx);
        }
    } else {
        (void)att_protocol_handle_line(s_serial_line, s_serial_send, s_serial_send_ctx);
    }
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
    att_protocol_set_config_apply(apply_runtime_config, NULL);

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

    uint32_t seq = 0u;
    status = append_attendance_record(&person.uid, person.sid, ATT_RECORD_NORMAL, now, &seq);
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:STORAGE\n");
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_error("STORAGE", now);
        return;
    }

    s_last_uid = person.uid;
    s_last_uid_time = now;
    s_last_uid_valid = 1u;

    char line[32];
    snprintf(line, sizeof(line), "ATTEND:OK:SEQ=%lu\n", (unsigned long)seq);
    send_line(line);
    emit_feedback(ATT_FEEDBACK_ATTEND_OK);
    att_display_show_attendance_ok(seq, person.sid, now);
}

void attendance_app_poll_serial(void)
{
    /* Serial bytes are dispatched by the STM32 UartDrv receive queue. */
}

void attendance_app_poll_network(void)
{
#if ATT_ENABLE_NETWORK
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

    if (!s_config.upload_enable) {
        return;
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
