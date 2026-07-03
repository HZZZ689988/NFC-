#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_display.h"
#include "GUI.h"

typedef struct {
    int row;
    const GUI_FONT *font;
    char text[32];
} gui_line_t;

typedef struct {
    unsigned short code;
    int x;
    int y;
} gui_char_t;

static gui_line_t g_lines[16];
static gui_char_t g_chars[16];
static unsigned g_line_count;
static unsigned g_char_count;
static unsigned g_update_count;

struct GUI_FONT {
    int dummy;
};

static const GUI_FONT g_default_font = {0};
static const GUI_FONT *g_current_font = &g_default_font;
const GUI_FONT GUI_FontHZ_SimSun_24 = {0};
const GUI_FONT GUI_Font8_ASCII = {0};

static void require_int(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static void reset_gui(void)
{
    memset(g_lines, 0, sizeof(g_lines));
    memset(g_chars, 0, sizeof(g_chars));
    g_line_count = 0u;
    g_char_count = 0u;
}

int GUI_Init(void)
{
    return 0;
}

void GUI_Clear(void)
{
    reset_gui();
}

void GUI_SetColor(int color)
{
    (void)color;
}

void GUI_DispStringAt(const char *text, int x, int y)
{
    (void)x;
    if (g_line_count < (sizeof(g_lines) / sizeof(g_lines[0]))) {
        g_lines[g_line_count].row = y;
        g_lines[g_line_count].font = g_current_font;
        snprintf(g_lines[g_line_count].text, sizeof(g_lines[g_line_count].text), "%s", text);
        g_line_count++;
    }
}

void GUI_DispCharAt(unsigned short c, int x, int y)
{
    if (g_char_count < (sizeof(g_chars) / sizeof(g_chars[0]))) {
        g_chars[g_char_count].code = c;
        g_chars[g_char_count].x = x;
        g_chars[g_char_count].y = y;
        g_char_count++;
    }
}

const GUI_FONT *GUI_SetFont(const GUI_FONT *font)
{
    const GUI_FONT *old_font = g_current_font;
    if (font != NULL) {
        g_current_font = font;
    }
    return old_font;
}

void GUI_Update(void)
{
    g_update_count++;
}

static const char *line_at(int row)
{
    for (unsigned i = 0u; i < g_line_count; ++i) {
        if (g_lines[i].row == row) {
            return g_lines[i].text;
        }
    }
    return "";
}

static void test_display_draws_ready_then_releases_event(void)
{
    att_device_config_t config;
    memset(&config, 0, sizeof(config));
    config.device_id = 7u;
    config.upload_enable = 1u;

    require_int(att_display_init() == ATT_OK, "display init should succeed");
    att_display_set_config(&config);
    att_display_set_record_count(2u);
    att_display_set_network(ATT_DISPLAY_NET_READY);
    att_display_set_weather("Hangzhou Sunny/20C");

    att_display_poll(10u);
    require_int(g_line_count == 2u, "ready page should use two 24px lines");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24, "ready page should use SimSun 24");
    require_int(strcmp(line_at(0), "TAP CARD") == 0, "ready page should show tap prompt");
    require_int(strcmp(line_at(32), "NET READY") == 0, "ready page should show network state");

    att_display_show_attendance_result(3u, 1001u, ATT_RECORD_IN, 11u, 0u, "OK", NULL);
    att_display_poll(11u);
    require_int(g_line_count == 2u, "attendance OK page 1 should show two 24px lines");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24,
                "attendance OK should use SimSun 24 detail font");
    require_int(strcmp(line_at(0), "00:00 IN") == 0,
                "attendance OK page 1 should show time and type");
    require_int(strcmp(line_at(32), "ID1001") == 0,
                "attendance OK page 1 should show SID");

    require_int(att_display_page_next() == 1u, "attendance detail should page next");
    att_display_poll(11u);
    require_int(strcmp(line_at(0), "STAT OK") == 0,
                "attendance OK page 2 should show status");
    require_int(strcmp(line_at(32), "SEQ 3") == 0,
                "attendance OK page 2 should show result");

    require_int(att_display_page_prev() == 1u, "attendance detail should page previous");
    att_display_poll(11u);
    require_int(strcmp(line_at(0), "00:00 IN") == 0,
                "attendance OK page previous should restore page 1");

    att_display_poll(40u);
    require_int(strcmp(line_at(0), "00:00 IN") == 0, "event page should hold before timeout");

    att_display_poll(41u);
    require_int(strcmp(line_at(0), "TAP CARD") == 0, "event page should release after timeout");
    require_int(strcmp(line_at(32), "NET READY") == 0, "ready page should restore network state");
    require_int(att_display_page_next() == 0u, "ready page should not consume page key");
}

static void test_display_draws_oled_gbk_demo_string(void)
{
    require_int(att_display_init() == ATT_OK, "display init should succeed");

    att_display_show_oled_test();
    att_display_poll(0u);

    require_int(g_line_count == 2u, "OLED test should draw two 24px lines");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24,
                "OLED test line 0 should draw with SimSun 24");
    require_int(g_lines[1].font == &GUI_FontHZ_SimSun_24,
                "OLED test line 1 should draw with SimSun 24");
    require_int(strcmp(g_lines[0].text, "24 OLED") == 0,
                "OLED test line 0 should draw 24px title");
    require_int(strcmp(g_lines[1].text, "OK TEST") == 0,
                "OLED test line 1 should draw 24px status");
    require_int(g_char_count == 0u, "OLED test should not use the direct character path");
}

static void test_display_draws_event_pages(void)
{
    require_int(att_display_init() == ATT_OK, "display init should succeed");

    att_display_show_attendance_duplicate(20u);
    att_display_poll(20u);
    require_int(strcmp(line_at(0), "00:00 DUP") == 0, "duplicate page should show time/status");
    require_int(strcmp(line_at(32), "ID----") == 0, "duplicate page should show empty SID");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24,
                "duplicate page should use SimSun 24 detail font");
    require_int(att_display_page_next() == 1u, "duplicate page should page next");
    att_display_poll(20u);
    require_int(strcmp(line_at(0), "STAT DUP") == 0, "duplicate page 2 should show status");
    require_int(strcmp(line_at(32), "WAIT") == 0, "duplicate page 2 should show result");

    att_display_show_attendance_invalid(21u);
    att_display_poll(21u);
    require_int(strcmp(line_at(0), "00:00 ERR") == 0, "invalid page should show time/status");
    require_int(strcmp(line_at(32), "ID----") == 0, "invalid page should show empty SID");
    require_int(att_display_page_next() == 1u, "invalid page should page next");
    att_display_poll(21u);
    require_int(strcmp(line_at(0), "STAT ERR") == 0, "invalid page 2 should show error status");
    require_int(strcmp(line_at(32), "BAD CARD") == 0, "invalid page 2 should show result");

    att_display_show_error("TEST", 22u);
    att_display_poll(22u);
    require_int(strcmp(line_at(0), "00:00 ERR") == 0, "error page should show time/status");
    require_int(att_display_page_next() == 1u, "error page should page next");
    att_display_poll(22u);
    require_int(strcmp(line_at(0), "STAT ERR") == 0, "error page 2 should show status");
    require_int(strcmp(line_at(32), "TEST") == 0, "error page 2 should show reason");
}

static void test_display_draws_out_duration_result(void)
{
    require_int(att_display_init() == ATT_OK, "display init should succeed");

    att_display_show_attendance_result(4u, 1001u, ATT_RECORD_OUT, 1200u, 1200u, "OK", NULL);
    att_display_poll(1200u);

    require_int(strcmp(line_at(0), "00:20 OUT") == 0, "OUT page 1 should show time/type");
    require_int(strcmp(line_at(32), "ID1001") == 0, "OUT page 1 should show SID");
    require_int(att_display_page_next() == 1u, "OUT page should page next");
    att_display_poll(1200u);
    require_int(strcmp(line_at(0), "STAT OK") == 0, "OUT page 2 should show status");
    require_int(strcmp(line_at(32), "DUR 0:20") == 0, "OUT page 2 should show duration result");
}

static void test_display_applies_timezone_for_valid_unix_time(void)
{
    att_device_config_t config;
    memset(&config, 0, sizeof(config));
    config.timezone = 8;

    require_int(att_display_init() == ATT_OK, "display init should succeed");
    att_display_set_config(&config);

    att_display_show_attendance_result(5u, 1001u, ATT_RECORD_IN,
                                       1783004400u, 0u, "OK", NULL);
    att_display_poll(1783004400u);

    require_int(strcmp(line_at(0), "23:00 IN") == 0,
                "valid Unix display time should apply UTC+8 timezone");
    require_int(strcmp(line_at(32), "ID1001") == 0,
                "timezone test should keep SID line");
}

static void test_display_draws_weather_page(void)
{
    require_int(att_display_init() == ATT_OK, "display init should succeed");

    att_display_set_weather("Sunny 20C");
    att_display_show_weather(30u);
    att_display_poll(30u);

    require_int(g_line_count == 2u, "weather page should use two 24px lines");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24,
                "weather page should use SimSun 24");
    require_int(strcmp(line_at(0), "WEATHER") == 0, "weather page should show title");
    require_int(strcmp(line_at(32), "Sunny 20C") == 0, "weather page should show cached text");
}

static void test_display_draws_admin_pages(void)
{
    require_int(att_display_init() == ATT_OK, "display init should succeed");

    att_display_show_admin(42u, ATT_MODE_CHECK_IN, 0u, NULL, 100u);
    att_display_poll(100u);
    require_int(g_line_count == 2u, "admin device page should use two 24px lines");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24,
                "admin page should use SimSun 24");
    require_int(strcmp(line_at(0), "ADMIN DEV") == 0,
                "admin device page should show field title");
    require_int(strcmp(line_at(32), "DEV 42") == 0,
                "admin device page should show device id");

    att_display_show_admin(42u, ATT_MODE_IN_OUT, 1u, NULL, 101u);
    att_display_poll(101u);
    require_int(strcmp(line_at(0), "ADMIN MODE") == 0,
                "admin mode page should show field title");
    require_int(strcmp(line_at(32), "MODE AUTO") == 0,
                "admin mode page should show in-out mode");

    att_display_show_admin(42u, ATT_MODE_IN_OUT, 1u, "DENY CARD", 102u);
    att_display_poll(102u);
    require_int(strcmp(line_at(0), "ADMIN") == 0,
                "admin message page should show admin title");
    require_int(strcmp(line_at(32), "DENY CARD") == 0,
                "admin message page should show message");
}

int main(void)
{
    test_display_draws_ready_then_releases_event();
    test_display_draws_oled_gbk_demo_string();
    test_display_draws_event_pages();
    test_display_draws_out_duration_result();
    test_display_applies_timezone_for_valid_unix_time();
    test_display_draws_weather_page();
    test_display_draws_admin_pages();
    return 0;
}
