#include "att_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_crc16.h"
#include "att_storage.h"

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

    if (strcmp(payload, "CFG?") == 0) {
        att_device_config_t config;
        if (att_storage_load_config(&config) != ATT_OK) {
            send("ERR:CFG\n", ctx);
            return ATT_ERR_STORAGE;
        }
        char response[128];
        snprintf(response, sizeof(response), "CFG:DEV=%lu|MODE=%u|UPLOAD=%u\n",
                 (unsigned long)config.device_id,
                 (unsigned int)config.work_mode,
                 (unsigned int)config.upload_enable);
        send(response, ctx);
        return ATT_OK;
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

        send("OK:ISSUE_ACCEPTED\n", ctx);
        return ATT_OK;
    }

    if (strncmp(payload, "LIST:", 5) == 0) {
        uint32_t count = 0u;
        if (att_storage_record_count(&count) != ATT_OK) {
            send("ERR:LIST\n", ctx);
            return ATT_ERR_STORAGE;
        }
        char response[48];
        snprintf(response, sizeof(response), "LIST:COUNT=%lu\n", (unsigned long)count);
        send(response, ctx);
        send("LIST:END\n", ctx);
        return ATT_OK;
    }

    send("ERR:UNKNOWN\n", ctx);
    return ATT_ERR;
}
