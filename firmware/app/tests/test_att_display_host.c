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
        g_lines[g_line_count].row = y / 8;
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
    require_int(strcmp(line_at(4), "NET READY") == 0, "ready page should show network state");

    att_display_show_attendance_ok(3u, 1001u, 11u);
    att_display_poll(11u);
    require_int(g_line_count == 2u, "attendance OK should use two 24px lines");
    require_int(strcmp(line_at(0), "OK") == 0, "attendance OK should show OK");
    require_int(strcmp(line_at(4), "ID1001") == 0, "attendance OK should show SID");

    att_display_poll(15u);
    require_int(strcmp(line_at(0), "OK") == 0, "event page should hold before timeout");

    att_display_poll(16u);
    require_int(strcmp(line_at(0), "TAP CARD") == 0, "event page should release after timeout");
    require_int(strcmp(line_at(4), "NET READY") == 0, "ready page should restore network state");
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
    require_int(strcmp(line_at(0), "DUP") == 0, "duplicate page should show DUP");
    require_int(strcmp(line_at(4), "WAIT") == 0, "duplicate page should show WAIT");
    require_int(g_lines[0].font == &GUI_FontHZ_SimSun_24,
                "duplicate page should use SimSun 24");

    att_display_show_attendance_invalid(21u);
    att_display_poll(21u);
    require_int(strcmp(line_at(0), "BAD CARD") == 0, "invalid page should show BAD CARD");
    require_int(strcmp(line_at(4), "CHECK") == 0, "invalid page should show CHECK");

    att_display_show_error("TEST", 22u);
    att_display_poll(22u);
    require_int(strcmp(line_at(0), "ERROR") == 0, "error page should show ERROR");
    require_int(strcmp(line_at(4), "TEST") == 0, "error page should show reason");
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
    require_int(strcmp(line_at(4), "Sunny 20C") == 0, "weather page should show cached text");
}

int main(void)
{
    test_display_draws_ready_then_releases_event();
    test_display_draws_oled_gbk_demo_string();
    test_display_draws_event_pages();
    test_display_draws_weather_page();
    return 0;
}
