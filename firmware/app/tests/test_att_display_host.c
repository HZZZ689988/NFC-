#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_display.h"
#include "GUI.h"

typedef struct {
    int row;
    char text[32];
} gui_line_t;

static gui_line_t g_lines[16];
static unsigned g_line_count;
static unsigned g_update_count;

struct GUI_FONT {
    int dummy;
};

static const GUI_FONT g_default_font = {0};
static const GUI_FONT *g_current_font = &g_default_font;
const GUI_FONT GUI_FontHZ_SimSun_12 = {0};

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
    g_line_count = 0u;
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
        snprintf(g_lines[g_line_count].text, sizeof(g_lines[g_line_count].text), "%s", text);
        g_line_count++;
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
    require_int(strcmp(line_at(0), "NFC Attend D7") == 0, "ready page should show device id");
    require_int(strcmp(line_at(1), "NET READY") == 0, "ready page should show network state");
    require_int(strcmp(line_at(2), "REC:2 UP:ON") == 0, "ready page should show records and upload flag");
    require_int(strcmp(line_at(3), "Hangzhou Sunny/20C") == 0, "ready page should show weather");
    require_int(strcmp(line_at(5), "READY 10s") == 0, "ready page should show current seconds");

    att_display_show_attendance_ok(3u, 1001u, 11u);
    att_display_poll(11u);
    require_int(strcmp(line_at(2), "REC:3 UP:ON") == 0, "attendance OK should increment record count");
    require_int(strcmp(line_at(5), "OK SEQ:3") == 0, "attendance OK should show seq");
    require_int(strcmp(line_at(6), "SID:1001") == 0, "attendance OK should show SID");

    att_display_poll(15u);
    require_int(strcmp(line_at(5), "OK SEQ:3") == 0, "event page should hold before timeout");

    att_display_poll(16u);
    require_int(strcmp(line_at(5), "READY 16s") == 0, "event page should release after timeout");
}

static void test_display_draws_oled_ascii_pattern(void)
{
    static const char gbk_hdu[] =
        "GBK:\xBA\xBC\xB5\xE7\xBF\xC6\xBC\xBC\xB4\xF3\xD1\xA7";
    static const char gbk_name[] =
        "\xD4\xF8\xD6\xDD\xD7\xD3\xD8\xB9";

    require_int(att_display_init() == ATT_OK, "display init should succeed");

    att_display_show_oled_test();

    require_int(strcmp(line_at(0), "OLED ASCII TEST") == 0, "OLED test should show title");
    require_int(strcmp(line_at(1), "ABCDEFGHIJKLMNO") == 0, "OLED test should show first alphabet row");
    require_int(strcmp(line_at(2), "PQRSTUVWXYZ0123") == 0, "OLED test should show second alphabet row");
    require_int(strcmp(line_at(3), "456789 !?:+-/") == 0, "OLED test should show punctuation row");
    require_int(strcmp(line_at(5), gbk_hdu) == 0, "OLED test should show GBK SimSun row");
    require_int(strcmp(line_at(6), gbk_name) == 0, "OLED test should show second GBK row");
}

int main(void)
{
    test_display_draws_ready_then_releases_event();
    test_display_draws_oled_ascii_pattern();
    return 0;
}
