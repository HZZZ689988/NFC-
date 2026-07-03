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
#define ATT_VALID_UNIX_MIN                  1609459200u
#define ATT_ADMIN_TIMEOUT_SEC               120u
#define ATT_ADMIN_DEVICE_ID_MAX             9999u
#define ATT_LRU_CACHE_SIZE                  8u

typedef struct {
    uint8_t valid;
    att_uid_t uid;
    att_record_t record;
    uint32_t age;
} attendance_lru_entry_t;

static att_device_config_t s_config;
static uint32_t s_next_seq = 1u;
static att_protocol_send_fn s_serial_send;
static void *s_serial_send_ctx;
static attendance_app_time_fn s_time_now;
static void *s_time_ctx;
static attendance_feedback_fn s_feedback;
static void *s_feedback_ctx;
static attendance_reset_fn s_reset;
static void *s_reset_ctx;
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
static uint8_t s_card_poll_paused;
static att_display_network_state_t s_network_display_state;
static uint8_t s_admin_active;
static uint8_t s_admin_field;
static uint32_t s_admin_last_action_sec;
static att_device_config_t s_admin_config;
static attendance_lru_entry_t s_lru_cache[ATT_LRU_CACHE_SIZE];
static uint32_t s_lru_clock;

static uint32_t app_now(void);
static uint8_t uid_equal(const att_uid_t *left, const att_uid_t *right);
static uint8_t is_duplicate_uid(const att_uid_t *uid, uint32_t now);
static void remember_presented_uid(const att_uid_t *uid, uint32_t now);
static void send_line(const char *line);
static void admin_check_timeout(uint32_t now);
static void admin_enter(uint32_t now);
static void admin_show(const char *message, uint32_t now);
static void lru_cache_clear(void);
static uint8_t lru_cache_lookup(const att_uid_t *uid, att_record_t *record);
static void lru_cache_put(const att_uid_t *uid, const att_record_t *record);

typedef enum {
    ATTEND_REJECT_NONE = 0,
    ATTEND_REJECT_ALREADY_IN,
    ATTEND_REJECT_NO_ENTRY,
} attendance_reject_t;

static void set_card_poll_paused(uint8_t paused)
{
    s_card_poll_paused = paused ? 1u : 0u;
    if (s_card_poll_paused == 0u) {
        s_last_uid_valid = 0u;
        s_last_uid_time = 0u;
        memset(&s_last_uid, 0, sizeof(s_last_uid));
    }
}

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

static void default_reset(void *ctx)
{
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

static att_work_mode_t admin_normalize_mode(att_work_mode_t mode)
{
    if (mode == ATT_MODE_CHECK_IN ||
        mode == ATT_MODE_CHECK_OUT ||
        mode == ATT_MODE_IN_OUT) {
        return mode;
    }

    return ATT_MODE_IN_OUT;
}

static att_work_mode_t admin_step_mode(att_work_mode_t mode, int8_t delta)
{
    uint8_t value = (uint8_t)admin_normalize_mode(mode);
    if (delta > 0) {
        value = value >= (uint8_t)ATT_MODE_IN_OUT ? (uint8_t)ATT_MODE_CHECK_IN : (uint8_t)(value + 1u);
    } else if (delta < 0) {
        value = value <= (uint8_t)ATT_MODE_CHECK_IN ? (uint8_t)ATT_MODE_IN_OUT : (uint8_t)(value - 1u);
    }
    return (att_work_mode_t)value;
}

static void admin_show(const char *message, uint32_t now)
{
    att_display_show_admin(s_admin_config.device_id,
                           admin_normalize_mode(s_admin_config.work_mode),
                           s_admin_field,
                           message,
                           now);
}

static void admin_enter(uint32_t now)
{
    s_admin_active = 1u;
    s_admin_field = 0u;
    s_admin_last_action_sec = now;
    s_admin_config = s_config;
    s_admin_config.work_mode = admin_normalize_mode(s_admin_config.work_mode);
    if (s_admin_config.device_id == 0u) {
        s_admin_config.device_id = 1u;
    } else if (s_admin_config.device_id > ATT_ADMIN_DEVICE_ID_MAX) {
        s_admin_config.device_id = ATT_ADMIN_DEVICE_ID_MAX;
    }
    admin_show(NULL, now);
}

static void admin_check_timeout(uint32_t now)
{
    if (s_admin_active == 0u) {
        return;
    }

    if ((uint32_t)(now - s_admin_last_action_sec) >= ATT_ADMIN_TIMEOUT_SEC) {
        s_admin_active = 0u;
        send_line("ADMIN:TIMEOUT\n");
        att_display_show_ready(now);
    }
}

static void lru_cache_clear(void)
{
    memset(s_lru_cache, 0, sizeof(s_lru_cache));
    s_lru_clock = 0u;
}

static uint8_t lru_cache_lookup(const att_uid_t *uid, att_record_t *record)
{
    if (uid == NULL || record == NULL) {
        return 0u;
    }

    for (uint8_t i = 0u; i < ATT_LRU_CACHE_SIZE; ++i) {
        if (s_lru_cache[i].valid != 0u && uid_equal(&s_lru_cache[i].uid, uid)) {
            s_lru_cache[i].age = ++s_lru_clock;
            *record = s_lru_cache[i].record;
            return 1u;
        }
    }

    return 0u;
}

static void lru_cache_put(const att_uid_t *uid, const att_record_t *record)
{
    if (uid == NULL || record == NULL) {
        return;
    }

    uint8_t slot = 0u;
    uint32_t oldest_age = UINT32_MAX;
    for (uint8_t i = 0u; i < ATT_LRU_CACHE_SIZE; ++i) {
        if (s_lru_cache[i].valid != 0u && uid_equal(&s_lru_cache[i].uid, uid)) {
            slot = i;
            oldest_age = 0u;
            break;
        }
        if (s_lru_cache[i].valid == 0u) {
            slot = i;
            oldest_age = 0u;
            break;
        }
        if (s_lru_cache[i].age < oldest_age) {
            oldest_age = s_lru_cache[i].age;
            slot = i;
        }
    }

    s_lru_cache[slot].valid = 1u;
    s_lru_cache[slot].uid = *uid;
    s_lru_cache[slot].record = *record;
    s_lru_cache[slot].age = ++s_lru_clock;
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

static uint8_t weather_text_is_valid(const char *text)
{
    if (text == NULL || *text == '\0') {
        return 0u;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20u || ch == '|') {
            return 0u;
        }
    }

    return 1u;
}

static const char *record_type_text(att_record_type_t type)
{
    switch (type) {
    case ATT_RECORD_IN:
        return "IN";
    case ATT_RECORD_OUT:
        return "OUT";
    default:
        return "NORMAL";
    }
}

static att_status_t find_latest_record_for_uid(const att_uid_t *uid,
                                               att_record_t *record,
                                               uint8_t *found)
{
    if (uid == NULL || record == NULL || found == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    *found = 0u;

    if (lru_cache_lookup(uid, record) != 0u) {
        *found = 1u;
        return ATT_OK;
    }

    uint32_t count = 0u;
    att_status_t status = att_storage_record_count(&count);
    if (status != ATT_OK) {
        return status;
    }

    for (uint32_t i = count; i > 0u; --i) {
        att_record_t candidate;
        status = att_storage_read_record(i - 1u, &candidate);
        if (status != ATT_OK) {
            return status;
        }

        if (uid_equal(&candidate.uid, uid)) {
            *record = candidate;
            *found = 1u;
            lru_cache_put(uid, record);
            return ATT_OK;
        }
    }

    return ATT_OK;
}

static att_status_t decide_attendance_record(const att_uid_t *uid, uint32_t now,
                                             att_record_type_t *record_type,
                                             uint32_t *duration_sec,
                                             attendance_reject_t *reject)
{
    if (uid == NULL || record_type == NULL || duration_sec == NULL || reject == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    *record_type = ATT_RECORD_NORMAL;
    *duration_sec = 0u;
    *reject = ATTEND_REJECT_NONE;

    if (s_config.work_mode == ATT_MODE_NORMAL) {
        return ATT_OK;
    }

    att_record_t latest;
    uint8_t found = 0u;
    att_status_t status = find_latest_record_for_uid(uid, &latest, &found);
    if (status != ATT_OK) {
        return status;
    }

    uint8_t is_inside = (uint8_t)(found != 0u && latest.type == ATT_RECORD_IN);

    switch (s_config.work_mode) {
    case ATT_MODE_CHECK_IN:
        if (is_inside != 0u) {
            *reject = ATTEND_REJECT_ALREADY_IN;
            return ATT_OK;
        }
        *record_type = ATT_RECORD_IN;
        return ATT_OK;
    case ATT_MODE_CHECK_OUT:
        if (is_inside == 0u) {
            *reject = ATTEND_REJECT_NO_ENTRY;
            return ATT_OK;
        }
        *record_type = ATT_RECORD_OUT;
        *duration_sec = (uint32_t)(now - latest.timestamp);
        return ATT_OK;
    case ATT_MODE_IN_OUT:
        if (is_inside != 0u) {
            *record_type = ATT_RECORD_OUT;
            *duration_sec = (uint32_t)(now - latest.timestamp);
        } else {
            *record_type = ATT_RECORD_IN;
        }
        return ATT_OK;
    case ATT_MODE_NORMAL:
    default:
        *record_type = ATT_RECORD_NORMAL;
        return ATT_OK;
    }
}

static void copy_weather_text(char *dest, size_t dest_len, const char *src)
{
    if (dest == NULL || dest_len == 0u) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    size_t i = 0u;
    while (i < dest_len - 1u && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
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
    lru_cache_put(uid, &record);
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
    if (is_duplicate_uid(&uid, now)) {
        *sid_out = sid;
        *seq_out = 0u;
        emit_feedback(ATT_FEEDBACK_ATTEND_DUPLICATE);
        att_display_show_attendance_duplicate(now);
        return ATT_ERR_DUPLICATE;
    }

    att_status_t status = append_attendance_record(&uid, sid,
                                                   (att_record_type_t)type_value,
                                                   now, seq_out);
    if (status != ATT_OK) {
        return status;
    }

    *sid_out = sid;
    s_last_uid = uid;
    s_last_uid_time = now;
    s_last_uid_valid = 1u;
    emit_feedback(ATT_FEEDBACK_ATTEND_OK);
    att_display_show_attendance_result(*seq_out, sid, (att_record_type_t)type_value,
                                       now, 0u, "OK", NULL);
    return ATT_OK;
}

static att_status_t handle_ui_test(const char *payload)
{
    static const char prefix[] = "UITEST:";
    if (payload == NULL || strncmp(payload, prefix, sizeof(prefix) - 1u) != 0) {
        return ATT_ERR_INVALID_ARG;
    }

    const char *mode = payload + sizeof(prefix) - 1u;
    uint32_t now = app_now();

    if (strcmp(mode, "READY") == 0) {
        att_display_show_ready(now);
        return ATT_OK;
    }
    if (strcmp(mode, "OK") == 0) {
        emit_feedback(ATT_FEEDBACK_ATTEND_OK);
        att_display_show_attendance_result(s_next_seq, 1001u, ATT_RECORD_IN,
                                           now, 0u, "OK", NULL);
        return ATT_OK;
    }
    if (strcmp(mode, "DUP") == 0) {
        emit_feedback(ATT_FEEDBACK_ATTEND_DUPLICATE);
        att_display_show_attendance_duplicate(now);
        return ATT_OK;
    }
    if (strcmp(mode, "INVALID") == 0) {
        emit_feedback(ATT_FEEDBACK_CARD_INVALID);
        att_display_show_attendance_invalid(now);
        return ATT_OK;
    }
    if (strcmp(mode, "ERROR") == 0) {
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_error("TEST", now);
        return ATT_OK;
    }
    if (strcmp(mode, "NETOK") == 0) {
        set_network_state(ATT_DISPLAY_NET_ONLINE);
        return ATT_OK;
    }
    if (strcmp(mode, "NETERR") == 0) {
        set_network_state(ATT_DISPLAY_NET_ERROR);
        return ATT_OK;
    }

    return ATT_ERR_INVALID_ARG;
}

static att_status_t load_weather_text(char *weather, size_t weather_len)
{
    att_status_t status = att_storage_load_weather(weather, weather_len);
    if (status == ATT_ERR_STORAGE || (status == ATT_OK && weather[0] == '\0')) {
        copy_weather_text(weather, weather_len, "WEATHER --");
        return ATT_OK;
    }
    return status;
}

static att_status_t save_weather_text(const char *weather)
{
    if (weather_text_is_valid(weather) == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    char stored[ATT_WEATHER_TEXT_LEN];
    copy_weather_text(stored, sizeof(stored), weather);
    att_status_t status = att_storage_save_weather(stored);
    if (status != ATT_OK) {
        return status;
    }

    att_display_set_weather(stored);
    att_display_show_weather(app_now());
    return ATT_OK;
}

static att_status_t query_weather_now(char *weather, size_t weather_len)
{
#if ATT_ENABLE_NETWORK
    if (s_network_ready == 0u) {
        return ATT_ERR_NOT_READY;
    }

    att_status_t status = att_network_query_weather(weather, weather_len);
    if (status != ATT_OK || weather[0] == '\0') {
        return status == ATT_OK ? ATT_ERR : status;
    }

    status = att_storage_save_weather(weather);
    if (status != ATT_OK) {
        return status;
    }

    att_display_set_weather(weather);
    att_display_show_weather(app_now());
    s_last_weather_time = app_now();
    s_weather_due = 0u;
    set_network_state(ATT_DISPLAY_NET_ONLINE);
    return ATT_OK;
#else
    (void)weather;
    (void)weather_len;
    return ATT_ERR_NOT_READY;
#endif
}

#if ATT_ENABLE_NETWORK
static const char *network_state_text(uint8_t state)
{
    switch (state) {
    case 0u:
        return "IDLE";
    case 1u:
        return "AT_OK";
    case 2u:
        return "WIFI_CONNECTING";
    case 3u:
        return "WIFI_CONNECTED";
    case 4u:
        return "TCP_CONNECTING";
    case 5u:
        return "TCP_CONNECTED";
    case 6u:
        return "TRANSPARENT";
    default:
        return "UNKNOWN";
    }
}

static const char *ota_state_text(att_ota_state_t state)
{
    switch (state) {
    case ATT_OTA_STATE_IDLE:
        return "IDLE";
    case ATT_OTA_STATE_NONE:
        return "NONE";
    case ATT_OTA_STATE_AVAILABLE:
        return "AVAILABLE";
    case ATT_OTA_STATE_DOWNLOADING:
        return "DOWNLOADING";
    case ATT_OTA_STATE_READY:
        return "READY";
    case ATT_OTA_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
#endif

static void send_weather_response(const char *weather)
{
    char response[64];
    snprintf(response, sizeof(response), "WEATHER:%s\n", weather != NULL ? weather : "");
    s_serial_send(response, s_serial_send_ctx);
}

static void send_time_response(void)
{
    uint32_t now = app_now();
    char response[64];
    snprintf(response, sizeof(response), "TIME:%lu|VALID=%u\n",
             (unsigned long)now,
             (unsigned int)(now >= ATT_VALID_UNIX_MIN ? 1u : 0u));
    s_serial_send(response, s_serial_send_ctx);
}

#if ATT_ENABLE_NETWORK
static void send_network_response(void)
{
    att_network_status_t status;
    if (att_network_get_status(&status) != ATT_OK) {
        s_serial_send("ERR:NET\n", s_serial_send_ctx);
        return;
    }

    uint8_t link_ready = (status.esp_state == 6u) ? 1u : 0u;
    char response[256];
    snprintf(response, sizeof(response),
             "NET:READY=%u|CFG=%u|UPLOAD=%u|STATE=%u|TEXT=%s|NTP=%u|BL=%u|DEV=%lu|HOST=%s|PORT=%u|SSID=%s\n",
             (unsigned int)link_ready,
             (unsigned int)status.configured,
             (unsigned int)status.upload_enabled,
             (unsigned int)status.esp_state,
             network_state_text(status.esp_state),
             (unsigned int)status.ntp_synced,
             (unsigned int)status.blacklist_count,
             (unsigned long)status.device_id,
             status.server_host,
             (unsigned int)status.server_port,
             status.ssid);
    s_serial_send(response, s_serial_send_ctx);
}

static void send_ota_response(void)
{
    att_ota_status_t status;
    if (att_network_get_ota_status(&status) != ATT_OK) {
        s_serial_send("ERR:OTA\n", s_serial_send_ctx);
        return;
    }

    att_ota_file_status_t cache;
    att_status_t cache_status = att_storage_ota_status(&cache);
    if ((status.state == ATT_OTA_STATE_IDLE ||
         status.state == ATT_OTA_STATE_NONE ||
         status.state == ATT_OTA_STATE_ERROR) &&
        cache_status == ATT_OK &&
        cache.valid != 0u) {
        char response[224];
        snprintf(response, sizeof(response),
                 "OTA:STATE=CACHED|OK=%u|VER=%s|CUR=%c|SLOT=%c|RX=%lu|SIZE=%lu|CRC32=%08lX|ACT=%08lX|TARGET=%08lX|INSTALL=%lu|ERR=%lu\n",
                 (unsigned int)cache.verified,
                 cache.version,
                 cache.current_slot == 2u ? 'B' : cache.current_slot == 1u ? 'A' : '-',
                 cache.target_slot == 2u ? 'B' : cache.target_slot == 1u ? 'A' : '-',
                 (unsigned long)cache.received_bytes,
                 (unsigned long)cache.size_bytes,
                 (unsigned long)cache.expected_crc32,
                 (unsigned long)cache.actual_crc32,
                 (unsigned long)cache.target_addr,
                 (unsigned long)cache.install_state,
                 (unsigned long)cache.install_error);
        s_serial_send(response, s_serial_send_ctx);
        return;
    }

    char response[176];
    snprintf(response, sizeof(response),
             "OTA:STATE=%s|VER=%s|SLOT=%c|RX=%lu|SIZE=%lu|CRC32=%08lX|ACT=%08lX\n",
             ota_state_text(status.state),
             status.version,
             status.target_slot == 2u ? 'B' : status.target_slot == 1u ? 'A' : '-',
             (unsigned long)status.received_bytes,
             (unsigned long)status.size_bytes,
             (unsigned long)status.expected_crc32,
             (unsigned long)status.actual_crc32);
    s_serial_send(response, s_serial_send_ctx);
}

static uint8_t ota_cache_is_ready_to_install(void)
{
    att_ota_file_status_t cache;
    if (att_storage_ota_status(&cache) != ATT_OK) {
        return 0u;
    }

    if (cache.valid == 0u ||
        cache.verified == 0u ||
        cache.size_bytes == 0u ||
        cache.received_bytes != cache.size_bytes ||
        cache.size_bytes > ATT_APP_SLOT_SIZE_BYTES ||
        cache.expected_crc32 != cache.actual_crc32 ||
        cache.target_addr != ATT_APP_SLOT_BASE_ADDR ||
        cache.target_slot != ATT_OTA_TARGET_SLOT_ID ||
        cache.install_error != ATT_OTA_INSTALL_ERR_NONE) {
        return 0u;
    }

    return (uint8_t)(cache.install_state == ATT_OTA_INSTALL_PENDING ||
                     cache.install_state == ATT_OTA_INSTALL_INSTALLING);
}

static void request_ota_install_reset(void)
{
    if (!ota_cache_is_ready_to_install()) {
        s_serial_send("ERR:OTA_NOT_READY\n", s_serial_send_ctx);
        return;
    }

    s_serial_send("OK:OTARST\n", s_serial_send_ctx);
    if (s_reset == NULL) {
        s_reset = default_reset;
        s_reset_ctx = NULL;
    }
    s_reset(s_reset_ctx);
}
#endif

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
    att_status_t parsed = att_protocol_parse_frame(s_serial_line, payload, sizeof(payload));
    if (parsed == ATT_OK && strncmp(payload, "SIMATT:", 7) == 0) {
        uint32_t seq = 0u;
        uint32_t sid = 0u;
        att_status_t status = handle_sim_attendance(payload, &seq, &sid);
        if (status == ATT_OK) {
            char response[32];
            snprintf(response, sizeof(response), "OK:SIMATT:SEQ=%lu\n", (unsigned long)seq);
            s_serial_send(response, s_serial_send_ctx);
        } else if (status == ATT_ERR_DUPLICATE) {
            s_serial_send("ERR:DUPLICATE\n", s_serial_send_ctx);
        } else if (status == ATT_ERR_STORAGE) {
            s_serial_send("ERR:STORAGE\n", s_serial_send_ctx);
        } else {
            s_serial_send("ERR:ARG\n", s_serial_send_ctx);
        }
    } else if (parsed == ATT_OK && strncmp(payload, "UITEST:", 7) == 0) {
        att_status_t status = handle_ui_test(payload);
        s_serial_send(status == ATT_OK ? "OK:UITEST\n" : "ERR:ARG\n", s_serial_send_ctx);
    } else if (parsed == ATT_OK && strcmp(payload, "WEATHER?") == 0) {
        char weather[ATT_WEATHER_TEXT_LEN];
        att_status_t status = load_weather_text(weather, sizeof(weather));
        if (status == ATT_OK) {
            att_display_set_weather(weather);
            att_display_show_weather(app_now());
            send_weather_response(weather);
        } else {
            s_serial_send("ERR:WEATHER\n", s_serial_send_ctx);
        }
    } else if (parsed == ATT_OK && strncmp(payload, "WEATHERTEST:", 12) == 0) {
        att_status_t status = save_weather_text(payload + 12u);
        s_serial_send(status == ATT_OK ? "OK:WEATHERTEST\n" :
                      status == ATT_ERR_STORAGE ? "ERR:WEATHER\n" : "ERR:ARG\n",
                      s_serial_send_ctx);
    } else if (parsed == ATT_OK && strcmp(payload, "WEATHER!") == 0) {
        char weather[ATT_WEATHER_TEXT_LEN];
        att_status_t status = query_weather_now(weather, sizeof(weather));
        if (status == ATT_OK) {
            send_weather_response(weather);
        } else if (status == ATT_ERR_NOT_READY) {
            s_serial_send("ERR:NOT_READY\n", s_serial_send_ctx);
        } else if (status == ATT_ERR_STORAGE) {
            s_serial_send("ERR:WEATHER\n", s_serial_send_ctx);
        } else {
            s_serial_send("ERR:WEATHER\n", s_serial_send_ctx);
        }
    } else if (parsed == ATT_OK && strcmp(payload, "TIME?") == 0) {
        send_time_response();
#if ATT_ENABLE_NETWORK
    } else if (parsed == ATT_OK && strcmp(payload, "NET?") == 0) {
        send_network_response();
    } else if (parsed == ATT_OK && strcmp(payload, "OTA?") == 0) {
        send_ota_response();
    } else if (parsed == ATT_OK && strcmp(payload, "OTA!") == 0) {
        att_status_t status = att_network_query_ota();
        s_serial_send(status == ATT_OK ? "OK:OTA\n" :
                      status == ATT_ERR_NOT_READY ? "ERR:NOT_READY\n" : "ERR:OTA\n",
                      s_serial_send_ctx);
    } else if (parsed == ATT_OK && strcmp(payload, "OTARST") == 0) {
        request_ota_install_reset();
#endif
    } else if (parsed == ATT_OK && strcmp(payload, "CARDLOCK:ON") == 0) {
        set_card_poll_paused(1u);
        s_serial_send("OK:CARDLOCK:ON\n", s_serial_send_ctx);
    } else if (parsed == ATT_OK && strcmp(payload, "CARDLOCK:OFF") == 0) {
        set_card_poll_paused(0u);
        s_serial_send("OK:CARDLOCK:OFF\n", s_serial_send_ctx);
    } else if (parsed == ATT_OK &&
               (strcmp(payload, "CARDLOCK?") == 0 || strcmp(payload, "CARDLOCK:?") == 0)) {
        s_serial_send(s_card_poll_paused ? "CARDLOCK:ON\n" : "CARDLOCK:OFF\n", s_serial_send_ctx);
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

static void remember_presented_uid(const att_uid_t *uid, uint32_t now)
{
    if (uid == NULL) {
        return;
    }

    s_last_uid = *uid;
    s_last_uid_time = now;
    s_last_uid_valid = 1u;
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
    (void)att_storage_boot_confirm_current();

    status = att_storage_load_config(&s_config);
    if (status != ATT_OK) {
        att_storage_default_config(&s_config);
        (void)att_storage_save_config(&s_config);
    }

    uint32_t count = 0u;
    if (att_storage_record_count(&count) == ATT_OK) {
        s_next_seq = count + 1u;
        if (count > 0u) {
            att_record_t latest;
            if (att_storage_read_record(count - 1u, &latest) == ATT_OK) {
                s_next_seq = latest.seq + 1u;
            }
        }
    }

    s_serial_send = default_serial_send;
    s_serial_send_ctx = NULL;
    s_time_now = default_time_now;
    s_time_ctx = NULL;
    s_feedback = default_feedback;
    s_feedback_ctx = NULL;
    s_reset = default_reset;
    s_reset_ctx = NULL;
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
    s_card_poll_paused = 0u;
    s_last_network_upload_time = 0u;
    s_last_network_heartbeat_time = 0u;
    s_last_network_time_sync_time = 0u;
    s_last_weather_time = 0u;
    s_network_upload_due = 1u;
    s_network_heartbeat_due = 1u;
    s_network_time_sync_due = 1u;
    s_weather_due = 1u;
    s_admin_active = 0u;
    s_admin_field = 0u;
    s_admin_last_action_sec = 0u;
    memset(&s_admin_config, 0, sizeof(s_admin_config));
    lru_cache_clear();
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

void attendance_app_set_reset(attendance_reset_fn reset, void *ctx)
{
    s_reset = (reset != NULL) ? reset : default_reset;
    s_reset_ctx = ctx;
}

int8_t attendance_app_get_timezone(void)
{
    return s_config.timezone;
}

uint8_t attendance_app_admin_is_active(void)
{
    return s_admin_active;
}

attendance_admin_result_t attendance_app_admin_handle_action(attendance_admin_action_t action)
{
    uint32_t now = app_now();
    admin_check_timeout(now);
    if (s_admin_active == 0u) {
        return ATT_ADMIN_RESULT_IGNORED;
    }

    s_admin_last_action_sec = now;

    switch (action) {
    case ATT_ADMIN_ACTION_PREV_FIELD:
    case ATT_ADMIN_ACTION_NEXT_FIELD:
        s_admin_field = s_admin_field == 0u ? 1u : 0u;
        admin_show(NULL, now);
        return ATT_ADMIN_RESULT_HANDLED;

    case ATT_ADMIN_ACTION_DEC:
        if (s_admin_field == 0u) {
            if (s_admin_config.device_id > 1u) {
                s_admin_config.device_id--;
            }
        } else {
            s_admin_config.work_mode = admin_step_mode(s_admin_config.work_mode, -1);
        }
        admin_show(NULL, now);
        return ATT_ADMIN_RESULT_HANDLED;

    case ATT_ADMIN_ACTION_INC:
        if (s_admin_field == 0u) {
            if (s_admin_config.device_id < ATT_ADMIN_DEVICE_ID_MAX) {
                s_admin_config.device_id++;
            }
        } else {
            s_admin_config.work_mode = admin_step_mode(s_admin_config.work_mode, 1);
        }
        admin_show(NULL, now);
        return ATT_ADMIN_RESULT_HANDLED;

    case ATT_ADMIN_ACTION_SAVE:
        if (att_storage_save_config(&s_admin_config) != ATT_OK) {
            admin_show("SAVE ERR", now);
            send_line("ADMIN:ERR:SAVE\n");
            return ATT_ADMIN_RESULT_HANDLED;
        }
        s_config = s_admin_config;
        att_display_set_config(&s_config);
        s_admin_active = 0u;
        admin_show("SAVE RESET", now);
        send_line("ADMIN:SAVED\n");
        return ATT_ADMIN_RESULT_SAVED;

    case ATT_ADMIN_ACTION_EXIT:
        s_admin_active = 0u;
        att_display_show_ready(now);
        send_line("ADMIN:EXIT\n");
        return ATT_ADMIN_RESULT_HANDLED;

    default:
        return ATT_ADMIN_RESULT_IGNORED;
    }
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
    uint32_t now = app_now();
    admin_check_timeout(now);

    if (s_card_poll_paused != 0u) {
        return;
    }

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

    if (person.card_type == ATT_CARD_ADMIN) {
        if (is_duplicate_uid(&person.uid, now)) {
            return;
        }

        admin_enter(now);
        remember_presented_uid(&person.uid, now);
        send_line("ADMIN:ON\n");
        emit_feedback(ATT_FEEDBACK_ATTEND_OK);
        return;
    }

    if (s_admin_active != 0u) {
        if (is_duplicate_uid(&person.uid, now)) {
            return;
        }

        remember_presented_uid(&person.uid, now);
        send_line("ADMIN:ERR:CARD_DENIED\n");
        emit_feedback(ATT_FEEDBACK_CARD_INVALID);
        att_display_show_admin(s_admin_config.device_id,
                               admin_normalize_mode(s_admin_config.work_mode),
                               s_admin_field,
                               "DENY CARD",
                               now);
        return;
    }

#if ATT_ENABLE_NETWORK
    if (att_network_uid_is_blacklisted(&person.uid) != 0u) {
        remember_presented_uid(&person.uid, now);
        send_line("ATTEND:ERR:BLACKLIST\n");
        emit_feedback(ATT_FEEDBACK_CARD_INVALID);
        att_display_show_attendance_result(0u, person.sid, ATT_RECORD_NORMAL,
                                           now, 0u, "ERR", "BLACKLIST");
        return;
    }
#endif

    if (is_duplicate_uid(&person.uid, now)) {
        send_line("ATTEND:SKIP:DUPLICATE\n");
        emit_feedback(ATT_FEEDBACK_ATTEND_DUPLICATE);
        att_display_show_attendance_duplicate(now);
        return;
    }

    uint32_t seq = 0u;
    att_record_type_t record_type = ATT_RECORD_NORMAL;
    uint32_t duration_sec = 0u;
    attendance_reject_t reject = ATTEND_REJECT_NONE;
    status = decide_attendance_record(&person.uid, now, &record_type, &duration_sec, &reject);
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:STORAGE\n");
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_error("STORAGE", now);
        return;
    }

    if (reject == ATTEND_REJECT_ALREADY_IN) {
        send_line("ATTEND:ERR:ALREADY_IN\n");
        emit_feedback(ATT_FEEDBACK_ATTEND_DUPLICATE);
        att_display_show_attendance_result(0u, person.sid, ATT_RECORD_IN,
                                           now, 0u, "DUP", "ALREADY IN");
        remember_presented_uid(&person.uid, now);
        return;
    }

    if (reject == ATTEND_REJECT_NO_ENTRY) {
        send_line("ATTEND:ERR:NO_ENTRY\n");
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_attendance_result(0u, person.sid, ATT_RECORD_OUT,
                                           now, 0u, "ERR", "NO ENTRY");
        remember_presented_uid(&person.uid, now);
        return;
    }

    status = append_attendance_record(&person.uid, person.sid, record_type, now, &seq);
    if (status != ATT_OK) {
        send_line("ATTEND:ERR:STORAGE\n");
        emit_feedback(ATT_FEEDBACK_ERROR);
        att_display_show_error("STORAGE", now);
        return;
    }

    remember_presented_uid(&person.uid, now);

    char line[64];
    if (record_type == ATT_RECORD_OUT) {
        snprintf(line, sizeof(line), "ATTEND:OK:SEQ=%lu|TYPE=%s|DUR=%lu\n",
                 (unsigned long)seq,
                 record_type_text(record_type),
                 (unsigned long)duration_sec);
    } else {
        snprintf(line, sizeof(line), "ATTEND:OK:SEQ=%lu|TYPE=%s\n",
                 (unsigned long)seq,
                 record_type_text(record_type));
    }
    send_line(line);
    emit_feedback(ATT_FEEDBACK_ATTEND_OK);
    att_display_show_attendance_result(seq, person.sid, record_type,
                                       now, duration_sec, "OK", NULL);
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

    uint8_t upload_busy = 0u;
    if (!s_config.upload_enable) {
        upload_busy = 0u;
    } else {
        if (s_network_heartbeat_due ||
            (uint32_t)(now - s_last_network_heartbeat_time) >= ATT_NETWORK_HEARTBEAT_INTERVAL_SEC) {
            (void)att_network_send_heartbeat();
            (void)att_network_query_blacklist();
            s_last_network_heartbeat_time = now;
            s_network_heartbeat_due = 0u;
            set_network_state(ATT_DISPLAY_NET_ONLINE);
        }

        if (s_network_upload_due ||
            (uint32_t)(now - s_last_network_upload_time) >= ATT_NETWORK_UPLOAD_INTERVAL_SEC) {
            att_status_t upload_status = att_network_upload_pending_batch(3u);
            s_last_network_upload_time = now;
            s_network_upload_due = 0u;
            if (upload_status == ATT_OK) {
                upload_busy = 1u;
                set_network_state(ATT_DISPLAY_NET_UPLOAD);
            } else {
                set_network_state(ATT_DISPLAY_NET_ONLINE);
            }
        }
    }

    if (!upload_busy &&
        (s_weather_due ||
         (uint32_t)(now - s_last_weather_time) >= ATT_NETWORK_WEATHER_INTERVAL_SEC)) {
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
        } else if (!s_config.upload_enable) {
            set_network_state(ATT_DISPLAY_NET_ERROR);
        }
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
