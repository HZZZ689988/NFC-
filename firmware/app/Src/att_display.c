#include "att_display.h"

#include <stdio.h>
#include <string.h>

#ifndef ATT_ENABLE_DISPLAY
#define ATT_ENABLE_DISPLAY 0
#endif

#if ATT_ENABLE_DISPLAY
#include "GUI.h"
#endif

#define ATT_DISPLAY_MESSAGE_LEN 24u
#define ATT_DISPLAY_WEATHER_LEN 24u
#define ATT_DISPLAY_EVENT_HOLD_SEC 5u

typedef enum {
    ATT_DISPLAY_EVENT_READY = 0,
    ATT_DISPLAY_EVENT_ATTEND_OK,
    ATT_DISPLAY_EVENT_DUPLICATE,
    ATT_DISPLAY_EVENT_INVALID,
    ATT_DISPLAY_EVENT_ERROR,
    ATT_DISPLAY_EVENT_OLED_TEST,
} att_display_event_t;

typedef struct {
    uint8_t initialized;
    uint8_t dirty;
    att_display_event_t event;
    att_display_network_state_t network;
    uint32_t event_time_sec;
    uint32_t device_id;
    uint32_t record_count;
    uint32_t last_seq;
    uint32_t last_sid;
    uint8_t upload_enable;
    char message[ATT_DISPLAY_MESSAGE_LEN];
    char weather[ATT_DISPLAY_WEATHER_LEN];
} att_display_state_t;

static att_display_state_t s_display;

static const char *network_text(att_display_network_state_t state)
{
    switch (state) {
    case ATT_DISPLAY_NET_OFF:
        return "NET OFF";
    case ATT_DISPLAY_NET_STARTING:
        return "NET START";
    case ATT_DISPLAY_NET_READY:
        return "NET READY";
    case ATT_DISPLAY_NET_ONLINE:
        return "NET OK";
    case ATT_DISPLAY_NET_UPLOAD:
        return "UPLOADING";
    case ATT_DISPLAY_NET_ERROR:
        return "NET ERROR";
    default:
        return "NET ?";
    }
}

static void copy_text(char *dest, size_t dest_len, const char *src)
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

static void mark_dirty(void)
{
    s_display.dirty = 1u;
}

static void set_event(att_display_event_t event, uint32_t now_sec)
{
    s_display.event = event;
    s_display.event_time_sec = now_sec;
    mark_dirty();
}

#if ATT_ENABLE_DISPLAY
static void draw_line(uint8_t row, const char *text)
{
    GUI_DispStringAt(text, 0, (int)row * 8);
}

static void draw_oled_test_screen(void)
{
    GUI_Clear();
    GUI_SetColor(GUI_COLOR_WHITE);

    draw_line(0u, "OLED ASCII TEST");
    draw_line(1u, "0123456789");
    draw_line(2u, "NET READY");
    draw_line(3u, "REC:0001 UP:ON");
    draw_line(4u, "TAP CARD");
    draw_line(5u, "OK SEQ:123");
    draw_line(6u, "SID:1001");
    draw_line(7u, "ERROR? NONE");
    GUI_Update();
}

static void draw_status_screen(uint32_t now_sec)
{
    char line[32];

    if (s_display.event == ATT_DISPLAY_EVENT_OLED_TEST) {
        draw_oled_test_screen();
        return;
    }

    GUI_Clear();
    GUI_SetColor(GUI_COLOR_WHITE);

    snprintf(line, sizeof(line), "NFC Attend D%lu", (unsigned long)s_display.device_id);
    draw_line(0u, line);

    draw_line(1u, network_text(s_display.network));

    snprintf(line, sizeof(line), "REC:%lu UP:%s",
             (unsigned long)s_display.record_count,
             s_display.upload_enable ? "ON" : "OFF");
    draw_line(2u, line);

    if (s_display.weather[0] != '\0') {
        draw_line(3u, s_display.weather);
    } else {
        draw_line(3u, "WEATHER --");
    }

    switch (s_display.event) {
    case ATT_DISPLAY_EVENT_ATTEND_OK:
        snprintf(line, sizeof(line), "OK SEQ:%lu", (unsigned long)s_display.last_seq);
        draw_line(5u, line);
        snprintf(line, sizeof(line), "SID:%lu", (unsigned long)s_display.last_sid);
        draw_line(6u, line);
        break;
    case ATT_DISPLAY_EVENT_DUPLICATE:
        draw_line(5u, "DUPLICATE");
        draw_line(6u, "TRY LATER");
        break;
    case ATT_DISPLAY_EVENT_INVALID:
        draw_line(5u, "INVALID CARD");
        draw_line(6u, "CHECK CRC/UID");
        break;
    case ATT_DISPLAY_EVENT_ERROR:
        draw_line(5u, "ERROR");
        draw_line(6u, s_display.message[0] ? s_display.message : "UNKNOWN");
        break;
    case ATT_DISPLAY_EVENT_READY:
    default:
        snprintf(line, sizeof(line), "READY %lus", (unsigned long)now_sec);
        draw_line(5u, line);
        draw_line(6u, "TAP CARD");
        break;
    }

    GUI_Update();
}
#endif

att_status_t att_display_init(void)
{
    if (s_display.initialized != 0u) {
        return ATT_OK;
    }

    s_display.event = ATT_DISPLAY_EVENT_READY;
    if (s_display.weather[0] == '\0') {
        copy_text(s_display.weather, sizeof(s_display.weather), "WEATHER --");
    }

#if ATT_ENABLE_DISPLAY
    if (GUI_Init() != 0) {
        return ATT_ERR;
    }
    s_display.initialized = 1u;
    s_display.dirty = 1u;
    draw_status_screen(0u);
#else
    s_display.initialized = 1u;
    s_display.dirty = 0u;
#endif
    return ATT_OK;
}

void att_display_set_config(const att_device_config_t *config)
{
    if (config == NULL) {
        return;
    }

    s_display.device_id = config->device_id;
    s_display.upload_enable = config->upload_enable ? 1u : 0u;
    mark_dirty();
}

void att_display_set_record_count(uint32_t count)
{
    if (s_display.record_count == count) {
        return;
    }
    s_display.record_count = count;
    mark_dirty();
}

void att_display_set_network(att_display_network_state_t state)
{
    if (s_display.network == state) {
        return;
    }
    s_display.network = state;
    mark_dirty();
}

void att_display_set_weather(const char *text)
{
    copy_text(s_display.weather, sizeof(s_display.weather), text);
    mark_dirty();
}

void att_display_show_ready(uint32_t now_sec)
{
    set_event(ATT_DISPLAY_EVENT_READY, now_sec);
}

void att_display_show_attendance_ok(uint32_t seq, uint32_t sid, uint32_t now_sec)
{
    s_display.last_seq = seq;
    s_display.last_sid = sid;
    s_display.record_count++;
    set_event(ATT_DISPLAY_EVENT_ATTEND_OK, now_sec);
}

void att_display_show_attendance_duplicate(uint32_t now_sec)
{
    set_event(ATT_DISPLAY_EVENT_DUPLICATE, now_sec);
}

void att_display_show_attendance_invalid(uint32_t now_sec)
{
    set_event(ATT_DISPLAY_EVENT_INVALID, now_sec);
}

void att_display_show_error(const char *reason, uint32_t now_sec)
{
    copy_text(s_display.message, sizeof(s_display.message), reason);
    set_event(ATT_DISPLAY_EVENT_ERROR, now_sec);
}

void att_display_show_oled_test(void)
{
    if (s_display.initialized == 0u) {
        return;
    }

    s_display.event = ATT_DISPLAY_EVENT_OLED_TEST;
    mark_dirty();
}

void att_display_poll(uint32_t now_sec)
{
    if (s_display.initialized == 0u) {
        return;
    }

    if (s_display.event != ATT_DISPLAY_EVENT_READY &&
        s_display.event != ATT_DISPLAY_EVENT_OLED_TEST &&
        (uint32_t)(now_sec - s_display.event_time_sec) >= ATT_DISPLAY_EVENT_HOLD_SEC) {
        s_display.event = ATT_DISPLAY_EVENT_READY;
        mark_dirty();
    }

#if ATT_ENABLE_DISPLAY
    if (s_display.dirty != 0u) {
        draw_status_screen(now_sec);
        s_display.dirty = 0u;
    }
#else
    (void)now_sec;
    s_display.dirty = 0u;
#endif
}
