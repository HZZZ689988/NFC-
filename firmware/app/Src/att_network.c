#include "att_network.h"

#include <stdio.h>
#include <string.h>

#include "att_crc16.h"
#include "att_crc32.h"
#include "att_storage.h"
#include "att_temperature.h"
#include "esp01s.h"

#define ATT_NETWORK_RX_LINE_MAX 511u
#define ATT_NETWORK_BLACKLIST_MAX 16u
#define ATT_NETWORK_UPLOAD_BATCH_MAX 3u
#define ATT_NETWORK_OTA_CHUNK_MAX 192u

static att_device_config_t s_config;
static uint8_t s_configured;
static char s_rx_line[ATT_NETWORK_RX_LINE_MAX + 1u];
static size_t s_rx_line_len;
static uint8_t s_rx_line_overflow;
static att_uid_t s_blacklist[ATT_NETWORK_BLACKLIST_MAX];
static uint8_t s_blacklist_count;
static att_ota_status_t s_ota_status;

static void network_rx_callback(const uint8_t *data, uint16_t len, void *ctx);

static att_status_t copy_config_string(char *dest, size_t dest_len, const char *src)
{
    if (dest == NULL || src == NULL || dest_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    size_t i = 0u;
    while (i < dest_len - 1u && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    if (src[i] != '\0') {
        dest[0] = '\0';
        return ATT_ERR_INVALID_ARG;
    }

    dest[i] = '\0';
    return ATT_OK;
}

static uint8_t parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return 0u;
    }

    uint32_t parsed = 0u;
    const char *p = text;
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return 0u;
        }

        uint32_t digit = (uint32_t)(*p - '0');
        if (parsed > (UINT32_MAX - digit) / 10u) {
            return 0u;
        }
        parsed = (parsed * 10u) + digit;
        p++;
    }

    *value = parsed;
    return 1u;
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

static uint8_t parse_hex16_4(const char *text, uint16_t *value)
{
    if (text == NULL || value == NULL) {
        return 0u;
    }

    uint16_t parsed = 0u;
    for (uint8_t i = 0u; i < 4u; ++i) {
        int nibble = hex_to_nibble(text[i]);
        if (nibble < 0) {
            return 0u;
        }
        parsed = (uint16_t)((parsed << 4) | (uint16_t)nibble);
    }
    *value = parsed;
    return 1u;
}

static uint8_t parse_hex32_8(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL) {
        return 0u;
    }

    uint32_t parsed = 0u;
    for (uint8_t i = 0u; i < 8u; ++i) {
        int nibble = hex_to_nibble(text[i]);
        if (nibble < 0) {
            return 0u;
        }
        parsed = (parsed << 4) | (uint32_t)nibble;
    }
    *value = parsed;
    return 1u;
}

static const char *find_field_value(const char *line, const char *key)
{
    if (line == NULL || key == NULL) {
        return NULL;
    }

    size_t key_len = strlen(key);
    const char *cursor = line;
    while ((cursor = strstr(cursor, key)) != NULL) {
        if ((cursor == line || cursor[-1] == '|') && cursor[key_len] == '=') {
            return cursor + key_len + 1u;
        }
        cursor += key_len;
    }
    return NULL;
}

static uint8_t parse_field_u32(const char *line, const char *key, uint32_t *value)
{
    const char *field = find_field_value(line, key);
    if (field == NULL || value == NULL) {
        return 0u;
    }

    char text[12];
    size_t i = 0u;
    while (i < sizeof(text) - 1u && field[i] != '\0' && field[i] != '|') {
        text[i] = field[i];
        i++;
    }
    text[i] = '\0';
    return parse_u32(text, value);
}

static uint8_t parse_field_hex32(const char *line, const char *key, uint32_t *value)
{
    const char *field = find_field_value(line, key);
    return field != NULL ? parse_hex32_8(field, value) : 0u;
}

static void copy_field_text(const char *line, const char *key, char *dest, size_t dest_len)
{
    if (dest == NULL || dest_len == 0u) {
        return;
    }
    dest[0] = '\0';

    const char *field = find_field_value(line, key);
    if (field == NULL) {
        return;
    }

    size_t i = 0u;
    while (i < dest_len - 1u && field[i] != '\0' && field[i] != '|') {
        dest[i] = field[i];
        i++;
    }
    dest[i] = '\0';
}

static uint8_t parse_field_slot(const char *line, const char *key, uint32_t *slot)
{
    const char *field = find_field_value(line, key);
    if (field == NULL || slot == NULL) {
        return 0u;
    }

    if ((field[1] != '\0' && field[1] != '|') ||
        (field[0] != 'A' && field[0] != 'B')) {
        return 0u;
    }

    *slot = field[0] == 'B' ? 2u : 1u;
    return 1u;
}

static att_status_t network_send_payload(const char *payload)
{
    if (payload == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    uint16_t crc = att_crc16_ccitt_false(payload, strlen(payload));
    char frame[384];
    int written = snprintf(frame, sizeof(frame), "CRC:%s*%04X\n", payload, crc);
    if (written < 0 || (size_t)written >= sizeof(frame)) {
        return ATT_ERR;
    }
    ESP01S_SendStr(frame);
    return ATT_OK;
}

static att_status_t request_ota_chunk(uint32_t offset)
{
    if (s_ota_status.size_bytes == 0u || offset >= s_ota_status.size_bytes) {
        return ATT_ERR_INVALID_ARG;
    }

    uint32_t remaining = s_ota_status.size_bytes - offset;
    uint32_t length = remaining > s_ota_status.chunk_size ? s_ota_status.chunk_size : remaining;
    if (length == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    char request[80];
    snprintf(request, sizeof(request), "OTA:GET:OFFSET=%lu|LEN=%lu|SLOT=%c",
             (unsigned long)offset,
             (unsigned long)length,
             ATT_OTA_TARGET_SLOT_ID == 2u ? 'B' : 'A');
    return network_send_payload(request);
}

static void set_ota_error(void)
{
    s_ota_status.state = ATT_OTA_STATE_ERROR;
}

static uint8_t decode_hex_bytes(const char *hex, uint8_t *dest, uint32_t len)
{
    if (hex == NULL || dest == NULL) {
        return 0u;
    }

    for (uint32_t i = 0u; i < len; ++i) {
        int hi = hex_to_nibble(hex[i * 2u]);
        int lo = hex_to_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return 0u;
        }
        dest[i] = (uint8_t)((hi << 4) | lo);
    }

    return (hex[len * 2u] == '\0' || hex[len * 2u] == '|') ? 1u : 0u;
}

static void complete_ota_download(void)
{
    uint32_t actual_crc32 = 0u;
    if (att_storage_ota_verify(&actual_crc32) == ATT_OK &&
        actual_crc32 == s_ota_status.expected_crc32) {
        s_ota_status.actual_crc32 = actual_crc32;
        s_ota_status.received_bytes = s_ota_status.size_bytes;
        s_ota_status.state = ATT_OTA_STATE_READY;
    } else {
        s_ota_status.actual_crc32 = actual_crc32;
        set_ota_error();
    }
}

static void handle_ota_available(const char *fields)
{
    char version[sizeof(s_ota_status.version)];
    uint32_t size_bytes = 0u;
    uint32_t expected_crc32 = 0u;
    uint32_t chunk_size = 0u;
    uint32_t advertised_slot = 0u;
    const char *slot_field = find_field_value(fields, "SLOT");
    uint8_t has_slot = slot_field != NULL ? parse_field_slot(fields, "SLOT", &advertised_slot) : 0u;

    copy_field_text(fields, "VERSION", version, sizeof(version));
    if (version[0] == '\0' ||
        parse_field_u32(fields, "SIZE", &size_bytes) == 0u ||
        parse_field_hex32(fields, "CRC32", &expected_crc32) == 0u ||
        parse_field_u32(fields, "CHUNK", &chunk_size) == 0u ||
        size_bytes == 0u ||
        chunk_size == 0u ||
        chunk_size > ATT_NETWORK_OTA_CHUNK_MAX ||
        (slot_field != NULL && has_slot == 0u) ||
        (has_slot != 0u && advertised_slot != ATT_OTA_TARGET_SLOT_ID)) {
        set_ota_error();
        return;
    }

    att_ota_file_status_t cached;
    if (att_storage_ota_status(&cached) == ATT_OK &&
        cached.valid != 0u &&
        cached.verified != 0u &&
        cached.size_bytes == size_bytes &&
        cached.expected_crc32 == expected_crc32 &&
        cached.target_slot == ATT_OTA_TARGET_SLOT_ID &&
        cached.target_addr == ATT_OTA_TARGET_BASE_ADDR &&
        strcmp(cached.version, version) == 0) {
        memset(&s_ota_status, 0, sizeof(s_ota_status));
        s_ota_status.state = ATT_OTA_STATE_READY;
        snprintf(s_ota_status.version, sizeof(s_ota_status.version), "%s", version);
        s_ota_status.size_bytes = size_bytes;
        s_ota_status.received_bytes = cached.received_bytes;
        s_ota_status.expected_crc32 = expected_crc32;
        s_ota_status.actual_crc32 = cached.actual_crc32;
        s_ota_status.target_slot = cached.target_slot;
        s_ota_status.chunk_size = (uint16_t)chunk_size;
        return;
    }

    memset(&s_ota_status, 0, sizeof(s_ota_status));
    s_ota_status.state = ATT_OTA_STATE_AVAILABLE;
    snprintf(s_ota_status.version, sizeof(s_ota_status.version), "%s", version);
    s_ota_status.target_slot = ATT_OTA_TARGET_SLOT_ID;
    s_ota_status.size_bytes = size_bytes;
    s_ota_status.expected_crc32 = expected_crc32;
    s_ota_status.chunk_size = (uint16_t)chunk_size;

    if (att_storage_ota_begin(version, size_bytes, expected_crc32) != ATT_OK) {
        set_ota_error();
        return;
    }

    s_ota_status.state = ATT_OTA_STATE_DOWNLOADING;
    if (request_ota_chunk(0u) != ATT_OK) {
        set_ota_error();
    }
}

static void handle_ota_data(const char *fields)
{
    uint32_t offset = 0u;
    uint32_t len = 0u;
    uint32_t expected_chunk_crc32 = 0u;
    uint8_t buffer[ATT_NETWORK_OTA_CHUNK_MAX];
    const char *hex = find_field_value(fields, "HEX");

    if (s_ota_status.state != ATT_OTA_STATE_DOWNLOADING ||
        parse_field_u32(fields, "OFFSET", &offset) == 0u ||
        parse_field_u32(fields, "LEN", &len) == 0u ||
        parse_field_hex32(fields, "CRC32", &expected_chunk_crc32) == 0u ||
        hex == NULL ||
        len == 0u ||
        len > ATT_NETWORK_OTA_CHUNK_MAX ||
        offset != s_ota_status.received_bytes ||
        offset > s_ota_status.size_bytes ||
        len > (s_ota_status.size_bytes - offset) ||
        decode_hex_bytes(hex, buffer, len) == 0u ||
        att_crc32_ieee(buffer, len) != expected_chunk_crc32) {
        set_ota_error();
        return;
    }

    if (att_storage_ota_write(offset, buffer, len) != ATT_OK) {
        set_ota_error();
        return;
    }

    s_ota_status.received_bytes = offset + len;
    if (s_ota_status.received_bytes >= s_ota_status.size_bytes) {
        complete_ota_download();
        return;
    }

    if (request_ota_chunk(s_ota_status.received_bytes) != ATT_OK) {
        set_ota_error();
    }
}

static void handle_ota_end(void)
{
    if (s_ota_status.state == ATT_OTA_STATE_DOWNLOADING &&
        s_ota_status.received_bytes == s_ota_status.size_bytes) {
        complete_ota_download();
    } else {
        set_ota_error();
    }
}

static void format_temperature(char *dest, size_t dest_len)
{
    if (dest == NULL || dest_len == 0u) {
        return;
    }

    int16_t centi = 0;
    if (att_temperature_read_centi_c(&centi) != ATT_OK) {
        snprintf(dest, dest_len, "0");
        return;
    }

    int32_t tenths = centi;
    if (tenths >= 0) {
        tenths = (tenths + 5) / 10;
    } else {
        tenths = (tenths - 5) / 10;
    }

    int32_t whole = tenths / 10;
    int32_t frac = tenths % 10;
    if (frac < 0) {
        frac = -frac;
    }
    snprintf(dest, dest_len, "%ld.%ld", (long)whole, (long)frac);
}

static uint8_t parse_uid_hex_exact(const char *text, att_uid_t *uid)
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

static uint8_t uid_equal(const att_uid_t *left, const att_uid_t *right)
{
    return (left != NULL && right != NULL &&
            memcmp(left->bytes, right->bytes, ATT_UID_LEN) == 0) ? 1u : 0u;
}

static void handle_upload_ack_list(const char *text)
{
    const char *cursor = text;
    while (cursor != NULL && *cursor != '\0') {
        uint32_t seq = 0u;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        char value[12];
        size_t len = (size_t)(cursor - start);
        if (len > 0u && len < sizeof(value)) {
            memcpy(value, start, len);
            value[len] = '\0';
            if (parse_u32(value, &seq) != 0u) {
                (void)att_storage_mark_uploaded(seq);
            }
        }
        if (*cursor == ',') {
            cursor++;
        }
    }
}

static void handle_blacklist_line(const char *line)
{
    static const char uids_key[] = "UIDS=";
    const char *uids = strstr(line, uids_key);
    if (uids == NULL) {
        return;
    }
    uids += sizeof(uids_key) - 1u;

    s_blacklist_count = 0u;
    while (*uids != '\0' && s_blacklist_count < ATT_NETWORK_BLACKLIST_MAX) {
        att_uid_t uid;
        if (parse_uid_hex_exact(uids, &uid) == 0u) {
            break;
        }
        s_blacklist[s_blacklist_count++] = uid;
        uids += ATT_UID_HEX_LEN;
        if (*uids == ',') {
            uids++;
        } else {
            break;
        }
    }
}

static void compact_weather_day(const char *src, char *dest, size_t dest_len)
{
    if (dest == NULL || dest_len == 0u) {
        return;
    }

    dest[0] = '\0';
    if (src == NULL || *src == '\0') {
        return;
    }

    size_t i = 0u;
    while (i < dest_len - 1u && src[i] != '\0' && src[i] != ' ') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void handle_plain_rx_line(const char *line)
{
    static const char upload_ack_prefix[] = "ACK:UPLOAD:";
    static const char upload_batch_ack_prefix[] = "ACK:UPLOADB:";

    if (line == NULL) {
        return;
    }

    const char *batch_ack = strstr(line, upload_batch_ack_prefix);
    if (batch_ack != NULL) {
        handle_upload_ack_list(batch_ack + sizeof(upload_batch_ack_prefix) - 1u);
        return;
    }

    const char *ack = strstr(line, upload_ack_prefix);
    if (ack != NULL) {
        uint32_t seq = 0u;
        if (parse_u32(ack + sizeof(upload_ack_prefix) - 1u, &seq) != 0u) {
            (void)att_storage_mark_uploaded(seq);
        }
        return;
    }

    if (strstr(line, "OTA:NONE") != NULL) {
        memset(&s_ota_status, 0, sizeof(s_ota_status));
        s_ota_status.state = ATT_OTA_STATE_NONE;
        s_ota_status.chunk_size = ATT_NETWORK_OTA_CHUNK_MAX;
        return;
    }

    const char *ota_data = strstr(line, "OTA:DATA:");
    if (ota_data != NULL) {
        handle_ota_data(ota_data + sizeof("OTA:DATA:") - 1u);
        return;
    }

    const char *ota_version = strstr(line, "OTA:VERSION=");
    if (ota_version != NULL) {
        handle_ota_available(ota_version + sizeof("OTA:") - 1u);
        return;
    }

    if (strstr(line, "OTA:END") != NULL) {
        handle_ota_end();
        return;
    }

    if (strstr(line, "OTA:ERR:") != NULL) {
        set_ota_error();
        return;
    }

    if (strstr(line, "BL:") != NULL) {
        handle_blacklist_line(line);
    }
}

static void handle_rx_line(const char *line)
{
    if (line == NULL) {
        return;
    }

    const char *crc_prefix = strstr(line, "CRC:");
    if (crc_prefix != NULL) {
        const char *payload = crc_prefix + 4;
        const char *star = strchr(payload, '*');
        if (star != NULL) {
            size_t payload_len = (size_t)(star - payload);
            uint16_t expected = 0u;
            char payload_copy[ATT_NETWORK_RX_LINE_MAX + 1u];
            if (payload_len <= ATT_NETWORK_RX_LINE_MAX &&
                parse_hex16_4(star + 1, &expected) != 0u &&
                att_crc16_ccitt_false(payload, payload_len) == expected) {
                memcpy(payload_copy, payload, payload_len);
                payload_copy[payload_len] = '\0';
                handle_plain_rx_line(payload_copy);
                return;
            }
        }
        return;
    }

    handle_plain_rx_line(line);
}

static void network_rx_callback(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    att_network_handle_rx(data, (size_t)len);
}

att_status_t att_network_init(const att_device_config_t *config)
{
    if (config == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    ESP01S_Config_t esp_cfg;
    memset(&esp_cfg, 0, sizeof(esp_cfg));
    if (copy_config_string(esp_cfg.ssid, sizeof(esp_cfg.ssid), config->wifi_ssid) != ATT_OK ||
        copy_config_string(esp_cfg.password, sizeof(esp_cfg.password), config->wifi_password) != ATT_OK ||
        copy_config_string(esp_cfg.tcpServerIP, sizeof(esp_cfg.tcpServerIP), config->server_host) != ATT_OK ||
        copy_config_string(esp_cfg.ntpServer, sizeof(esp_cfg.ntpServer), "ntp.aliyun.com") != ATT_OK) {
        return ATT_ERR_INVALID_ARG;
    }
    esp_cfg.tcpPort = config->server_port;
    esp_cfg.ntpTimezone = config->timezone;
    ESP01S_SetConfig(&esp_cfg);
    ESP01S_RegisterDataCb(network_rx_callback, NULL);

    s_config = *config;
    s_configured = 1u;
    s_rx_line_len = 0u;
    s_rx_line_overflow = 0u;
    s_blacklist_count = 0u;
    memset(&s_ota_status, 0, sizeof(s_ota_status));
    s_ota_status.state = ATT_OTA_STATE_IDLE;
    s_ota_status.chunk_size = ATT_NETWORK_OTA_CHUNK_MAX;

    return ATT_OK;
}

att_status_t att_network_sync_time(void)
{
    if (!s_configured) {
        return ATT_ERR_NOT_READY;
    }

    if (ESP01S_GetState() != ESP01S_STATE_TRANSPARENT) {
        ESP01S_SyncNtpTime();
    }
    return ESP01S_IsNtpSynced() ? ATT_OK : ATT_ERR;
}

att_status_t att_network_query_weather(char *text, size_t text_len)
{
    if (text == NULL || text_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    text[0] = '\0';

    if (!s_configured || s_config.weather_key[0] == '\0' || s_config.weather_location[0] == '\0') {
        return ATT_ERR_NOT_READY;
    }

    char city[16] = {0};
    char day[32] = {0};
    char high[8] = {0};
    char night[32] = {0};
    char low[8] = {0};
    char precip[8] = {0};
    int ret = ESP01S_QueryWeather(s_config.weather_key, s_config.weather_location,
                                  "en", "c", city, sizeof(city),
                                  day, sizeof(day), high, sizeof(high),
                                  night, sizeof(night), low, sizeof(low),
                                  precip, sizeof(precip));
    if (ret != 0) {
        return ATT_ERR;
    }

    char short_day[8];
    compact_weather_day(day, short_day, sizeof(short_day));
    snprintf(text, text_len, "%s %sC", short_day[0] ? short_day : "Weather", low);
    return ATT_OK;
}

att_status_t att_network_upload_pending(void)
{
    if (!s_configured || !s_config.upload_enable) {
        return ATT_ERR_NOT_READY;
    }

    att_record_t record;
    att_status_t status = att_storage_next_pending_upload(&record);
    if (status != ATT_OK) {
        return status;
    }

    char frame[160];
    snprintf(frame, sizeof(frame),
             "UPLOAD:SEQ=%lu|UID=%02X%02X%02X%02X|SID=%lu|TYPE=%u|TS=%lu|DEV=%lu",
             (unsigned long)record.seq,
             record.uid.bytes[0], record.uid.bytes[1], record.uid.bytes[2], record.uid.bytes[3],
             (unsigned long)record.sid,
             (unsigned int)record.type,
             (unsigned long)record.timestamp,
             (unsigned long)record.device_id);
    return network_send_payload(frame);
}

att_status_t att_network_upload_pending_batch(uint8_t max_records)
{
    if (!s_configured || !s_config.upload_enable) {
        return ATT_ERR_NOT_READY;
    }

    if (max_records == 0u || max_records > ATT_NETWORK_UPLOAD_BATCH_MAX) {
        max_records = ATT_NETWORK_UPLOAD_BATCH_MAX;
    }

    att_record_t records[ATT_NETWORK_UPLOAD_BATCH_MAX];
    uint8_t count = 0u;
    att_status_t status = att_storage_pending_uploads(records, max_records, &count);
    if (status != ATT_OK) {
        return status;
    }

    if (count == 1u) {
        char frame[160];
        const att_record_t *record = &records[0];
        snprintf(frame, sizeof(frame),
                 "UPLOAD:SEQ=%lu|UID=%02X%02X%02X%02X|SID=%lu|TYPE=%u|TS=%lu|DEV=%lu",
                 (unsigned long)record->seq,
                 record->uid.bytes[0], record->uid.bytes[1], record->uid.bytes[2], record->uid.bytes[3],
                 (unsigned long)record->sid,
                 (unsigned int)record->type,
                 (unsigned long)record->timestamp,
                 (unsigned long)record->device_id);
        return network_send_payload(frame);
    }

    char frame[320];
    int written = snprintf(frame, sizeof(frame), "UPLOADB:DEV=%lu|R=",
                           (unsigned long)s_config.device_id);
    if (written < 0 || (size_t)written >= sizeof(frame)) {
        return ATT_ERR;
    }
    size_t offset = (size_t)written;
    for (uint8_t i = 0u; i < count; ++i) {
        const att_record_t *record = &records[i];
        written = snprintf(frame + offset, sizeof(frame) - offset,
                           "%s%lu,%02X%02X%02X%02X,%lu,%u,%lu",
                           i == 0u ? "" : ";",
                           (unsigned long)record->seq,
                           record->uid.bytes[0], record->uid.bytes[1],
                           record->uid.bytes[2], record->uid.bytes[3],
                           (unsigned long)record->sid,
                           (unsigned int)record->type,
                           (unsigned long)record->timestamp);
        if (written < 0 || (size_t)written >= sizeof(frame) - offset) {
            return ATT_ERR;
        }
        offset += (size_t)written;
    }
    frame[offset] = '\0';
    return network_send_payload(frame);
}

att_status_t att_network_send_heartbeat(void)
{
    if (!s_configured || !s_config.upload_enable) {
        return ATT_ERR_NOT_READY;
    }

    char temp[12];
    format_temperature(temp, sizeof(temp));

    char frame[96];
    snprintf(frame, sizeof(frame), "HEARTBEAT:DEV=%lu|TEMP=%s",
             (unsigned long)s_config.device_id, temp);
    return network_send_payload(frame);
}

att_status_t att_network_query_blacklist(void)
{
    if (!s_configured || !s_config.upload_enable) {
        return ATT_ERR_NOT_READY;
    }

    return network_send_payload("BL?");
}

uint8_t att_network_uid_is_blacklisted(const att_uid_t *uid)
{
    if (uid == NULL) {
        return 0u;
    }
    for (uint8_t i = 0u; i < s_blacklist_count; ++i) {
        if (uid_equal(uid, &s_blacklist[i]) != 0u) {
            return 1u;
        }
    }
    return 0u;
}

att_status_t att_network_get_status(att_network_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    status->configured = s_configured;
    status->esp_state = (uint8_t)ESP01S_GetState();
    status->ntp_synced = ESP01S_IsNtpSynced() ? 1u : 0u;
    status->blacklist_count = s_blacklist_count;

    if (s_configured != 0u) {
        status->upload_enabled = s_config.upload_enable ? 1u : 0u;
        status->device_id = s_config.device_id;
        status->server_port = s_config.server_port;
        snprintf(status->ssid, sizeof(status->ssid), "%s", s_config.wifi_ssid);
        snprintf(status->server_host, sizeof(status->server_host), "%s", s_config.server_host);
    }

    return ATT_OK;
}

att_status_t att_network_query_ota(void)
{
    if (!s_configured) {
        return ATT_ERR_NOT_READY;
    }

    if (s_ota_status.state != ATT_OTA_STATE_DOWNLOADING) {
        s_ota_status.state = ATT_OTA_STATE_IDLE;
        s_ota_status.target_slot = ATT_OTA_TARGET_SLOT_ID;
        s_ota_status.chunk_size = ATT_NETWORK_OTA_CHUNK_MAX;
    }

    char request[24];
    snprintf(request, sizeof(request), "OTA?SLOT=%c", ATT_OTA_TARGET_SLOT_ID == 2u ? 'B' : 'A');
    return network_send_payload(request);
}

att_status_t att_network_get_ota_status(att_ota_status_t *status)
{
    if (status == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    *status = s_ota_status;
    return ATT_OK;
}

void att_network_handle_rx(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return;
    }

    for (size_t i = 0u; i < len; ++i) {
        uint8_t ch = data[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (s_rx_line_overflow == 0u && s_rx_line_len > 0u) {
                s_rx_line[s_rx_line_len] = '\0';
                handle_rx_line(s_rx_line);
            }
            s_rx_line_len = 0u;
            s_rx_line_overflow = 0u;
            continue;
        }

        if (s_rx_line_overflow != 0u) {
            continue;
        }

        if (s_rx_line_len >= ATT_NETWORK_RX_LINE_MAX) {
            s_rx_line_len = 0u;
            s_rx_line_overflow = 1u;
            continue;
        }

        s_rx_line[s_rx_line_len++] = (char)ch;
    }
}
