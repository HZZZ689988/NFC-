#include "att_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_card.h"
#include "att_crc16.h"
#include "att_display.h"
#include "att_storage.h"

static att_protocol_config_apply_fn s_config_apply;
static void *s_config_apply_ctx;

static int hex_to_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    ch = (char)toupper((unsigned char)ch);
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int parse_uid_hex(const char *hex, att_uid_t *uid)
{
    if (hex == NULL || uid == NULL || strlen(hex) != ATT_UID_HEX_LEN) {
        return -1;
    }

    for (size_t i = 0; i < ATT_UID_LEN; ++i) {
        int hi = hex_to_nibble(hex[i * 2u]);
        int lo = hex_to_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        uid->bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int parse_hex_block16(const char *hex, uint8_t out[16])
{
    if (hex == NULL || out == NULL || strlen(hex) != 32u) {
        return -1;
    }

    for (size_t i = 0; i < 16u; ++i) {
        int hi = hex_to_nibble(hex[i * 2u]);
        int lo = hex_to_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static uint8_t parse_u32_text(const char *text, uint32_t max_value, uint32_t *value)
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
        if (parsed > max_value) {
            return 0u;
        }
        p++;
    }

    *value = parsed;
    return 1u;
}

static uint8_t parse_i8_text(const char *text, int8_t min_value, int8_t max_value, int8_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return 0u;
    }

    int sign = 1;
    if (*text == '-') {
        sign = -1;
        text++;
    } else if (*text == '+') {
        text++;
    }

    uint32_t magnitude = 0u;
    if (parse_u32_text(text, 127u, &magnitude) == 0u) {
        return 0u;
    }

    int parsed = (int)magnitude * sign;
    if (parsed < (int)min_value || parsed > (int)max_value) {
        return 0u;
    }

    *value = (int8_t)parsed;
    return 1u;
}

static uint8_t config_text_is_valid(const char *text)
{
    if (text == NULL) {
        return 0u;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20u || ch == '|' || ch == '=') {
            return 0u;
        }
    }

    return 1u;
}

static att_status_t copy_config_text(char *dest, size_t dest_len, const char *src)
{
    if (dest == NULL || src == NULL || dest_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }
    if (config_text_is_valid(src) == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    size_t len = strlen(src);
    if (len >= dest_len) {
        return ATT_ERR_INVALID_ARG;
    }

    memcpy(dest, src, len + 1u);
    return ATT_OK;
}

static void uid_to_hex(const att_uid_t *uid, char out[ATT_UID_HEX_LEN + 1u])
{
    static const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < ATT_UID_LEN; ++i) {
        out[i * 2u] = hex[(uid->bytes[i] >> 4) & 0x0Fu];
        out[i * 2u + 1u] = hex[uid->bytes[i] & 0x0Fu];
    }
    out[ATT_UID_HEX_LEN] = '\0';
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

static const char *upload_state_text(att_upload_state_t state)
{
    switch (state) {
    case ATT_UPLOAD_DONE:
        return "DONE";
    case ATT_UPLOAD_FAILED:
        return "FAILED";
    default:
        return "PENDING";
    }
}

static void send_card_status(att_status_t status, const char *ok_line,
                             att_protocol_send_fn send, void *ctx)
{
    switch (status) {
    case ATT_OK:
        send(ok_line, ctx);
        break;
    case ATT_ERR_NO_CARD:
        send("ERR:NO_CARD\n", ctx);
        break;
    case ATT_ERR_CID_MISMATCH:
        send("ERR:UID_MISMATCH\n", ctx);
        break;
    case ATT_ERR_INVALID_ARG:
        send("ERR:ARG\n", ctx);
        break;
    case ATT_ERR_CRC:
        send("ERR:CRC\n", ctx);
        break;
    case ATT_ERR_NOT_READY:
        send("ERR:NOT_READY\n", ctx);
        break;
    default:
        send("ERR:CARD\n", ctx);
        break;
    }
}

static att_status_t apply_config_update(att_device_config_t *config)
{
    att_status_t status = att_storage_save_config(config);
    if (status != ATT_OK) {
        return status;
    }

    if (s_config_apply != NULL) {
        (void)s_config_apply(config, s_config_apply_ctx);
    }

    return ATT_OK;
}

static att_status_t handle_config_set(const char *payload)
{
    if (payload == NULL || strncmp(payload, "CFG:", 4) != 0) {
        return ATT_ERR_INVALID_ARG;
    }

    att_device_config_t config;
    att_status_t status = att_storage_load_config(&config);
    if (status != ATT_OK) {
        att_storage_default_config(&config);
    }

    const char *cursor = payload + 4;
    if (*cursor == '\0') {
        return ATT_ERR_INVALID_ARG;
    }

    while (*cursor != '\0') {
        const char *separator = strchr(cursor, '|');
        size_t token_len = separator == NULL ? strlen(cursor) : (size_t)(separator - cursor);
        char token[96];
        if (token_len == 0u || token_len >= sizeof(token)) {
            return ATT_ERR_INVALID_ARG;
        }

        memcpy(token, cursor, token_len);
        token[token_len] = '\0';

        char *equals = strchr(token, '=');
        if (equals == NULL || equals == token) {
            return ATT_ERR_INVALID_ARG;
        }

        *equals = '\0';
        const char *key = token;
        const char *value = equals + 1;
        uint32_t parsed = 0u;
        int8_t parsed_i8 = 0;

        if (strcmp(key, "DEV") == 0) {
            if (parse_u32_text(value, UINT32_MAX, &parsed) == 0u || parsed == 0u) {
                return ATT_ERR_INVALID_ARG;
            }
            config.device_id = parsed;
        } else if (strcmp(key, "MODE") == 0) {
            if (parse_u32_text(value, 3u, &parsed) == 0u) {
                return ATT_ERR_INVALID_ARG;
            }
            config.work_mode = (att_work_mode_t)parsed;
        } else if (strcmp(key, "UPLOAD") == 0) {
            if (parse_u32_text(value, 1u, &parsed) == 0u) {
                return ATT_ERR_INVALID_ARG;
            }
            config.upload_enable = (uint8_t)parsed;
        } else if (strcmp(key, "REPEAT") == 0) {
            if (parse_u32_text(value, UINT16_MAX, &parsed) == 0u) {
                return ATT_ERR_INVALID_ARG;
            }
            config.repeat_interval_sec = (uint16_t)parsed;
        } else if (strcmp(key, "SSID") == 0) {
            status = copy_config_text(config.wifi_ssid, sizeof(config.wifi_ssid), value);
            if (status != ATT_OK) {
                return status;
            }
        } else if (strcmp(key, "PWD") == 0) {
            status = copy_config_text(config.wifi_password, sizeof(config.wifi_password), value);
            if (status != ATT_OK) {
                return status;
            }
        } else if (strcmp(key, "HOST") == 0) {
            status = copy_config_text(config.server_host, sizeof(config.server_host), value);
            if (status != ATT_OK) {
                return status;
            }
        } else if (strcmp(key, "PORT") == 0) {
            if (parse_u32_text(value, UINT16_MAX, &parsed) == 0u || parsed == 0u) {
                return ATT_ERR_INVALID_ARG;
            }
            config.server_port = (uint16_t)parsed;
        } else if (strcmp(key, "WKEY") == 0) {
            status = copy_config_text(config.weather_key, sizeof(config.weather_key), value);
            if (status != ATT_OK) {
                return status;
            }
        } else if (strcmp(key, "WLOC") == 0) {
            status = copy_config_text(config.weather_location, sizeof(config.weather_location), value);
            if (status != ATT_OK) {
                return status;
            }
        } else if (strcmp(key, "TZ") == 0) {
            if (parse_i8_text(value, -12, 14, &parsed_i8) == 0u) {
                return ATT_ERR_INVALID_ARG;
            }
            config.timezone = parsed_i8;
        } else {
            return ATT_ERR_INVALID_ARG;
        }

        if (separator == NULL) {
            break;
        }
        cursor = separator + 1;
    }

    return apply_config_update(&config);
}

void att_protocol_set_config_apply(att_protocol_config_apply_fn apply, void *ctx)
{
    s_config_apply = apply;
    s_config_apply_ctx = ctx;
}

att_status_t att_protocol_build_frame(const char *payload, char *out, size_t out_len)
{
    if (payload == NULL || out == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    uint16_t crc = att_crc16_ccitt_false(payload, strlen(payload));
    int written = snprintf(out, out_len, "$%s*%04X\n", payload, crc);
    return (written > 0 && (size_t)written < out_len) ? ATT_OK : ATT_ERR_INVALID_ARG;
}

att_status_t att_protocol_parse_frame(const char *line, char *payload, size_t payload_len)
{
    if (line == NULL || payload == NULL || payload_len == 0u) {
        return ATT_ERR_INVALID_ARG;
    }

    if (line[0] != '$') {
        size_t len = strlen(line);
        if (len >= payload_len) {
            return ATT_ERR_INVALID_ARG;
        }
        memcpy(payload, line, len + 1u);
        return ATT_OK;
    }

    const char *star = strrchr(line, '*');
    if (star == NULL || strlen(star + 1) < 4u) {
        return ATT_ERR_CRC;
    }

    size_t len = (size_t)(star - line - 1);
    if (len >= payload_len) {
        return ATT_ERR_INVALID_ARG;
    }

    memcpy(payload, line + 1, len);
    payload[len] = '\0';

    char crc_text[5];
    memcpy(crc_text, star + 1, 4);
    crc_text[4] = '\0';
    uint16_t expected = (uint16_t)strtoul(crc_text, NULL, 16);
    uint16_t actual = att_crc16_ccitt_false(payload, len);
    return expected == actual ? ATT_OK : ATT_ERR_CRC;
}

att_status_t att_protocol_handle_line(const char *line, att_protocol_send_fn send, void *ctx)
{
    if (line == NULL || send == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    char payload[160];
    att_status_t parsed = att_protocol_parse_frame(line, payload, sizeof(payload));
    if (parsed != ATT_OK) {
        send("ERR:CRC\n", ctx);
        return parsed;
    }

    if (strcmp(payload, "PING") == 0) {
        send("OK:PONG\n", ctx);
        return ATT_OK;
    }

    if (strcmp(payload, "READ") == 0) {
        att_uid_t uid;
        att_status_t status = att_card_read_uid(&uid);
        if (status != ATT_OK) {
            send_card_status(status, "OK\n", send, ctx);
            return status;
        }

        char uid_hex[ATT_UID_HEX_LEN + 1u];
        uid_to_hex(&uid, uid_hex);
        char response[24];
        snprintf(response, sizeof(response), "UID:%s\n", uid_hex);
        send(response, ctx);
        return ATT_OK;
    }

    if (strcmp(payload, "CFG?") == 0) {
        att_device_config_t config;
        if (att_storage_load_config(&config) != ATT_OK) {
            send("ERR:CFG\n", ctx);
            return ATT_ERR_STORAGE;
        }
        char response[256];
        snprintf(response, sizeof(response),
                 "CFG:DEV=%lu|MODE=%u|UPLOAD=%u|REPEAT=%u|HOST=%s|PORT=%u|TZ=%d|SSID=%s|WLOC=%s\n",
                 (unsigned long)config.device_id,
                 (unsigned int)config.work_mode,
                 (unsigned int)config.upload_enable,
                 (unsigned int)config.repeat_interval_sec,
                 config.server_host,
                 (unsigned int)config.server_port,
                 (int)config.timezone,
                 config.wifi_ssid,
                 config.weather_location);
        send(response, ctx);
        return ATT_OK;
    }

    if (strcmp(payload, "DIAG?") == 0) {
        att_card_diag_t diag;
        att_status_t status = att_card_diag(&diag);
        if (status != ATT_OK) {
            send("ERR:DIAG\n", ctx);
            return status;
        }

        char response[192];
        snprintf(response, sizeof(response),
                 "DIAG:RC522_RAW=0x%02X|RC522_VER=0x%02X|CMD=0x%02X|IRQ=0x%02X|FIFO=0x%02X|TX=0x%02X|ERR=0x%02X|PINS=0x%02X|SHARE=%u|SPD=0x%02X->0x%02X->0x%02X|RW=%u|REQ=%d|TAG=%02X%02X\n",
                 diag.version_raw,
                 diag.version,
                 diag.command,
                 diag.com_irq,
                 diag.fifo_level,
                 diag.tx_control,
                 diag.error,
                 diag.pins,
                 (unsigned int)diag.shared_mosi_flash_cs,
                 diag.serial_speed_before,
                 diag.serial_speed_test,
                 diag.serial_speed_after,
                 (unsigned int)diag.spi_rw_ok,
                 (int)diag.request_status,
                 diag.tag_type[0],
                 diag.tag_type[1]);
        send(response, ctx);
        return ATT_OK;
    }

    if (strcmp(payload, "OLEDTEST") == 0) {
        att_display_show_oled_test();
        send("OK:OLEDTEST\n", ctx);
        return ATT_OK;
    }

    if (strncmp(payload, "CFG:", 4) == 0) {
        att_status_t status = handle_config_set(payload);
        switch (status) {
        case ATT_OK:
            send("OK:CFG\n", ctx);
            break;
        case ATT_ERR_STORAGE:
            send("ERR:CFG\n", ctx);
            break;
        default:
            send("ERR:ARG\n", ctx);
            break;
        }
        return status;
    }

    if (strncmp(payload, "ISSUE:", 6) == 0) {
        char uid_hex[ATT_UID_HEX_LEN + 1u];
        unsigned long sid = 0u;
        unsigned long points = 0u;
        unsigned int card_type = 0u;
        if (sscanf(payload + 6, "%8[0-9A-Fa-f],%lu,%lu,%u", uid_hex, &sid, &points, &card_type) != 4) {
            send("ERR:ARG\n", ctx);
            return ATT_ERR_INVALID_ARG;
        }

        att_uid_t uid;
        if (parse_uid_hex(uid_hex, &uid) != 0) {
            send("ERR:ARG\n", ctx);
            return ATT_ERR_INVALID_ARG;
        }

        att_person_t person;
        memset(&person, 0, sizeof(person));
        person.uid = uid;
        person.sid = (uint32_t)sid;
        person.points = (uint32_t)points;
        person.card_type = (att_card_type_t)card_type;

        att_status_t status = att_card_issue_checked(&person);
        send_card_status(status, "OK:ISSUE\n", send, ctx);
        return status;
    }

    if (strncmp(payload, "CLEAR:", 6) == 0) {
        att_uid_t uid;
        if (parse_uid_hex(payload + 6, &uid) != 0) {
            send("ERR:ARG\n", ctx);
            return ATT_ERR_INVALID_ARG;
        }

        att_status_t status = att_card_clear_checked(&uid);
        send_card_status(status, "OK:CLEAR\n", send, ctx);
        return status;
    }
    if ((strncmp(payload, "IMGA", 4) == 0) ||
        (strncmp(payload, "IMGN", 4) == 0) ||
        (strncmp(payload, "IMGD", 4) == 0)) {
        if (!isdigit((unsigned char)payload[4]) ||
            !isdigit((unsigned char)payload[5]) ||
            payload[6] != ':') {
            send("ERR:ARG\n", ctx);
            return ATT_ERR_INVALID_ARG;
        }

        att_card_image_area_t area = ATT_CARD_IMAGE_PORTRAIT;
        if (payload[3] == 'N') {
            area = ATT_CARD_IMAGE_NAME;
        } else if (payload[3] == 'D') {
            area = ATT_CARD_IMAGE_DEPARTMENT;
        }

        uint8_t index = (uint8_t)(((uint8_t)(payload[4] - '0') * 10u) + (uint8_t)(payload[5] - '0'));
        uint8_t block[16];
        if (parse_hex_block16(payload + 7, block) != 0) {
            send("ERR:ARG\n", ctx);
            return ATT_ERR_INVALID_ARG;
        }

        att_status_t status = att_card_write_image_block(area, index, block);
        send_card_status(status, "OK:IMG\n", send, ctx);
        return status;
    }

    if (strcmp(payload, "UPDATEIMG") == 0) {
        att_status_t status = att_card_finish_image_update();
        send_card_status(status, "OK:UPDATEIMG\n", send, ctx);
        return status;
    }

    if (strncmp(payload, "LIST:", 5) == 0) {
        uint32_t requested_limit = 0u;
        uint8_t list_all = (uint8_t)(strcmp(payload + 5, "ALL") == 0);
        if (!list_all) {
            char *end = NULL;
            unsigned long parsed_count = strtoul(payload + 5, &end, 10);
            if (end == payload + 5 || *end != '\0') {
                send("ERR:ARG\n", ctx);
                return ATT_ERR_INVALID_ARG;
            }
            requested_limit = (uint32_t)parsed_count;
        }

        uint32_t count = 0u;
        if (att_storage_record_count(&count) != ATT_OK) {
            send("ERR:LIST\n", ctx);
            return ATT_ERR_STORAGE;
        }

        char response[96];
        snprintf(response, sizeof(response), "LIST:COUNT=%lu\n", (unsigned long)count);
        send(response, ctx);

        uint32_t requested = list_all ? count : requested_limit;
        requested = (requested > count) ? count : requested;

        uint32_t start = count > requested ? count - requested : 0u;
        for (uint32_t i = start; i < count; ++i) {
            att_record_t record;
            att_status_t status = att_storage_read_record(i, &record);
            if (status != ATT_OK) {
                send("ERR:LIST\n", ctx);
                return status;
            }

            char uid_hex[ATT_UID_HEX_LEN + 1u];
            uid_to_hex(&record.uid, uid_hex);
            snprintf(response, sizeof(response),
                     "REC:SEQ=%lu|UID=%s|SID=%lu|%s|%lu|DEV=%lu|OK|UP=%s\n",
                     (unsigned long)record.seq,
                     uid_hex,
                     (unsigned long)record.sid,
                     record_type_text(record.type),
                     (unsigned long)record.timestamp,
                     (unsigned long)record.device_id,
                     upload_state_text(record.upload_state));
            send(response, ctx);
        }

        send("LIST:END\n", ctx);
        return ATT_OK;
    }

    send("ERR:UNKNOWN\n", ctx);
    return ATT_ERR;
}
