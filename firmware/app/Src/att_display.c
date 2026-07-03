#include "att_display.h"

#include <stdio.h>
#include <string.h>

#ifndef ATT_ENABLE_DISPLAY
#define ATT_ENABLE_DISPLAY 0
#endif

#if ATT_ENABLE_DISPLAY
#include "GUI.h"
extern GUI_FLASH const GUI_FONT GUI_Font8_ASCII;
extern GUI_FLASH const GUI_FONT GUI_FontHZ_SimSun_24;
#endif

#define ATT_DISPLAY_MESSAGE_LEN 24u
#define ATT_DISPLAY_STATUS_LEN  12u
#define ATT_DISPLAY_RESULT_LEN  24u
#define ATT_DISPLAY_WEATHER_LEN 24u
#define ATT_DISPLAY_EVENT_HOLD_SEC 5u
#define ATT_DISPLAY_DETAIL_HOLD_SEC 30u
#define ATT_DISPLAY_ADMIN_HOLD_SEC 120u
#define ATT_DISPLAY_VALID_UNIX_MIN 1609459200u

typedef enum {
    ATT_DISPLAY_EVENT_READY = 0,
    ATT_DISPLAY_EVENT_ATTEND_OK,
    ATT_DISPLAY_EVENT_DUPLICATE,
    ATT_DISPLAY_EVENT_INVALID,
    ATT_DISPLAY_EVENT_ERROR,
    ATT_DISPLAY_EVENT_WEATHER,
    ATT_DISPLAY_EVENT_OLED_TEST,
    ATT_DISPLAY_EVENT_ADMIN,
} att_display_event_t;

typedef struct {
    uint8_t initialized;
    uint8_t dirty;
    att_display_event_t event;
    att_display_network_state_t network;
    uint32_t event_time_sec;
    uint8_t detail_page;
    uint32_t device_id;
    uint32_t record_count;
    uint32_t last_seq;
    uint32_t last_sid;
    att_record_type_t last_record_type;
    uint8_t last_sid_valid;
    uint8_t last_type_valid;
    uint32_t last_duration_sec;
    uint8_t upload_enable;
    int8_t timezone;
    uint8_t admin_field;
    att_work_mode_t admin_mode;
    char status[ATT_DISPLAY_STATUS_LEN];
    char result[ATT_DISPLAY_RESULT_LEN];
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

static void format_clock(uint32_t now_sec, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0u) {
        return;
    }

    int32_t seconds = (int32_t)(now_sec % 86400u);
    if (now_sec >= ATT_DISPLAY_VALID_UNIX_MIN) {
        seconds += (int32_t)s_display.timezone * 3600;
    }
    while (seconds < 0) {
        seconds += 86400;
    }
    while (seconds >= 86400) {
        seconds -= 86400;
    }

    uint32_t hour = (uint32_t)seconds / 3600u;
    uint32_t minute = ((uint32_t)seconds % 3600u) / 60u;
    uint32_t second = (uint32_t)seconds % 60u;
    snprintf(out, out_len, "%02lu:%02lu:%02lu",
             (unsigned long)hour,
             (unsigned long)minute,
             (unsigned long)second);
}

static void format_duration(uint32_t duration_sec, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0u) {
        return;
    }

    uint32_t hours = duration_sec / 3600u;
    uint32_t minutes = (duration_sec % 3600u) / 60u;
    snprintf(out, out_len, "%lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes);
}

static void format_default_result(uint32_t seq, att_record_type_t type,
                                  uint32_t duration_sec,
                                  char *out, size_t out_len)
{
    if (out == NULL || out_len == 0u) {
        return;
    }

    if (type == ATT_RECORD_OUT) {
        char duration[16];
        format_duration(duration_sec, duration, sizeof(duration));
        snprintf(out, out_len, "DUR %s", duration);
        return;
    }

    snprintf(out, out_len, "SEQ %lu", (unsigned long)seq);
}

static void mark_dirty(void)
{
    s_display.dirty = 1u;
}

static void set_event(att_display_event_t event, uint32_t now_sec)
{
    s_display.event = event;
    s_display.event_time_sec = now_sec;
    s_display.detail_page = 0u;
    mark_dirty();
}

static uint8_t is_detail_event(att_display_event_t event)
{
    return (uint8_t)(event == ATT_DISPLAY_EVENT_ATTEND_OK ||
                     event == ATT_DISPLAY_EVENT_DUPLICATE ||
                     event == ATT_DISPLAY_EVENT_INVALID ||
                     event == ATT_DISPLAY_EVENT_ERROR);
}

static uint32_t event_hold_sec(att_display_event_t event)
{
    if (event == ATT_DISPLAY_EVENT_ADMIN) {
        return ATT_DISPLAY_ADMIN_HOLD_SEC;
    }
    return is_detail_event(event) != 0u ?
            ATT_DISPLAY_DETAIL_HOLD_SEC : ATT_DISPLAY_EVENT_HOLD_SEC;
}

#if ATT_ENABLE_DISPLAY
static void draw_big_line(uint8_t line, const char *text)
{
    GUI_DispStringAt(text, 0, line == 0u ? 0 : 32);
}

static const char *short_record_type_text(att_record_type_t type)
{
    switch (type) {
    case ATT_RECORD_IN:
        return "IN";
    case ATT_RECORD_OUT:
        return "OUT";
    default:
        return "NORM";
    }
}

static const char *mode_text(att_work_mode_t mode)
{
    switch (mode) {
    case ATT_MODE_CHECK_IN:
        return "IN";
    case ATT_MODE_CHECK_OUT:
        return "OUT";
    case ATT_MODE_IN_OUT:
        return "AUTO";
    default:
        return "NORM";
    }
}

static void draw_oled_test_screen(void)
{
    const GUI_FONT GUI_UNI_PTR *old_font;

    GUI_Clear();
    GUI_SetColor(GUI_COLOR_WHITE);

    old_font = GUI_SetFont(&GUI_FontHZ_SimSun_24);
    draw_big_line(0u, "24 OLED");
    draw_big_line(1u, "OK TEST");
    GUI_SetFont(old_font);
    GUI_Update();
}

static void draw_admin_screen(void)
{
    char line[32];
    const GUI_FONT GUI_UNI_PTR *old_font;

    GUI_Clear();
    GUI_SetColor(GUI_COLOR_WHITE);
    old_font = GUI_SetFont(&GUI_FontHZ_SimSun_24);

    if (s_display.message[0] != '\0') {
        draw_big_line(0u, "ADMIN");
        draw_big_line(1u, s_display.message);
    } else if (s_display.admin_field == 0u) {
        draw_big_line(0u, "ADMIN DEV");
        snprintf(line, sizeof(line), "DEV %lu", (unsigned long)s_display.device_id);
        draw_big_line(1u, line);
    } else {
        draw_big_line(0u, "ADMIN MODE");
        snprintf(line, sizeof(line), "MODE %s", mode_text(s_display.admin_mode));
        draw_big_line(1u, line);
    }

    GUI_SetFont(old_font);
    GUI_Update();
}

static void draw_attendance_detail_screen(uint32_t now_sec)
{
    char line[32];
    char time_text[12];
    const GUI_FONT GUI_UNI_PTR *old_font;

    (void)now_sec;

    GUI_Clear();
    GUI_SetColor(GUI_COLOR_WHITE);
    old_font = GUI_SetFont(&GUI_FontHZ_SimSun_24);

    if (s_display.detail_page == 0u) {
        format_clock(s_display.event_time_sec, time_text, sizeof(time_text));
        time_text[5] = '\0';
        snprintf(line, sizeof(line), "%s %s", time_text,
                 s_display.last_type_valid != 0u ?
                        short_record_type_text(s_display.last_record_type) :
                        (s_display.status[0] != '\0' ? s_display.status : "--"));
        draw_big_line(0u, line);

        if (s_display.last_sid_valid != 0u) {
            snprintf(line, sizeof(line), "ID%lu", (unsigned long)s_display.last_sid);
        } else {
            snprintf(line, sizeof(line), "ID----");
        }
        draw_big_line(1u, line);
    } else {
        snprintf(line, sizeof(line), "STAT %s",
                 s_display.status[0] != '\0' ? s_display.status : "--");
        draw_big_line(0u, line);

        snprintf(line, sizeof(line), "%s",
                 s_display.result[0] != '\0' ? s_display.result : "--");
        draw_big_line(1u, line);
    }

    GUI_SetFont(old_font);
    GUI_Update();
}

static void draw_status_screen(uint32_t now_sec)
{
    const GUI_FONT GUI_UNI_PTR *old_font;

    (void)now_sec;

    if (s_display.event == ATT_DISPLAY_EVENT_OLED_TEST) {
        draw_oled_test_screen();
        return;
    }
    if (s_display.event == ATT_DISPLAY_EVENT_ADMIN) {
        draw_admin_screen();
        return;
    }

    GUI_Clear();
    GUI_SetColor(GUI_COLOR_WHITE);
    old_font = GUI_SetFont(&GUI_FontHZ_SimSun_24);

    switch (s_display.event) {
    case ATT_DISPLAY_EVENT_ATTEND_OK:
        GUI_SetFont(old_font);
        draw_attendance_detail_screen(now_sec);
        return;
    case ATT_DISPLAY_EVENT_DUPLICATE:
    case ATT_DISPLAY_EVENT_INVALID:
    case ATT_DISPLAY_EVENT_ERROR:
        GUI_SetFont(old_font);
        draw_attendance_detail_screen(now_sec);
        return;
    case ATT_DISPLAY_EVENT_WEATHER:
        draw_big_line(0u, "WEATHER");
        draw_big_line(1u, s_display.weather[0] ? s_display.weather : "WEATHER --");
        break;
    case ATT_DISPLAY_EVENT_READY:
    default:
        draw_big_line(0u, "TAP CARD");
        draw_big_line(1u, network_text(s_display.network));
        break;
    }

    GUI_SetFont(old_font);
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
    s_display.timezone = config->timezone;
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
    att_display_show_attendance_result(seq, sid, ATT_RECORD_NORMAL, now_sec,
                                       0u, "OK", NULL);
}

void att_display_show_attendance_result(uint32_t seq, uint32_t sid,
                                        att_record_type_t record_type,
                                        uint32_t now_sec,
                                        uint32_t duration_sec,
                                        const char *status_text,
                                        const char *result_text)
{
    s_display.last_seq = seq;
    s_display.last_sid = sid;
    s_display.last_sid_valid = sid != 0u ? 1u : 0u;
    s_display.last_record_type = record_type;
    s_display.last_type_valid = 1u;
    s_display.last_duration_sec = duration_sec;
    copy_text(s_display.status, sizeof(s_display.status), status_text != NULL ? status_text : "OK");
    if (result_text != NULL && result_text[0] != '\0') {
        copy_text(s_display.result, sizeof(s_display.result), result_text);
    } else {
        format_default_result(seq, record_type, duration_sec,
                              s_display.result, sizeof(s_display.result));
    }
    if (seq > s_display.record_count) {
        s_display.record_count = seq;
    }
    set_event(ATT_DISPLAY_EVENT_ATTEND_OK, now_sec);
}

void att_display_show_attendance_duplicate(uint32_t now_sec)
{
    s_display.last_seq = 0u;
    s_display.last_sid = 0u;
    s_display.last_sid_valid = 0u;
    s_display.last_type_valid = 0u;
    s_display.last_duration_sec = 0u;
    copy_text(s_display.status, sizeof(s_display.status), "DUP");
    copy_text(s_display.result, sizeof(s_display.result), "WAIT");
    set_event(ATT_DISPLAY_EVENT_DUPLICATE, now_sec);
}

void att_display_show_attendance_invalid(uint32_t now_sec)
{
    s_display.last_seq = 0u;
    s_display.last_sid = 0u;
    s_display.last_sid_valid = 0u;
    s_display.last_type_valid = 0u;
    s_display.last_duration_sec = 0u;
    copy_text(s_display.status, sizeof(s_display.status), "ERR");
    copy_text(s_display.result, sizeof(s_display.result), "BAD CARD");
    set_event(ATT_DISPLAY_EVENT_INVALID, now_sec);
}

void att_display_show_error(const char *reason, uint32_t now_sec)
{
    copy_text(s_display.message, sizeof(s_display.message), reason);
    s_display.last_seq = 0u;
    s_display.last_sid = 0u;
    s_display.last_sid_valid = 0u;
    s_display.last_type_valid = 0u;
    s_display.last_duration_sec = 0u;
    copy_text(s_display.status, sizeof(s_display.status), "ERR");
    copy_text(s_display.result, sizeof(s_display.result),
              s_display.message[0] ? s_display.message : "UNKNOWN");
    set_event(ATT_DISPLAY_EVENT_ERROR, now_sec);
}

void att_display_show_weather(uint32_t now_sec)
{
    set_event(ATT_DISPLAY_EVENT_WEATHER, now_sec);
}

void att_display_show_oled_test(void)
{
    if (s_display.initialized == 0u) {
        return;
    }

    s_display.event = ATT_DISPLAY_EVENT_OLED_TEST;
    mark_dirty();
}

void att_display_show_admin(uint32_t device_id, att_work_mode_t mode,
                            uint8_t field, const char *message,
                            uint32_t now_sec)
{
    s_display.device_id = device_id;
    s_display.admin_mode = mode;
    s_display.admin_field = field != 0u ? 1u : 0u;
    copy_text(s_display.message, sizeof(s_display.message), message);
    set_event(ATT_DISPLAY_EVENT_ADMIN, now_sec);
}

uint8_t att_display_page_prev(void)
{
    if (is_detail_event(s_display.event) == 0u) {
        return 0u;
    }

    s_display.detail_page = s_display.detail_page == 0u ? 1u : 0u;
    mark_dirty();
    return 1u;
}

uint8_t att_display_page_next(void)
{
    if (is_detail_event(s_display.event) == 0u) {
        return 0u;
    }

    s_display.detail_page = s_display.detail_page == 0u ? 1u : 0u;
    mark_dirty();
    return 1u;
}

void att_display_poll(uint32_t now_sec)
{
    if (s_display.initialized == 0u) {
        return;
    }

    if (s_display.event != ATT_DISPLAY_EVENT_READY &&
        s_display.event != ATT_DISPLAY_EVENT_OLED_TEST &&
        (uint32_t)(now_sec - s_display.event_time_sec) >= event_hold_sec(s_display.event)) {
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
