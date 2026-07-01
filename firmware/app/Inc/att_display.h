#ifndef ATT_DISPLAY_H
#define ATT_DISPLAY_H

#include <stdint.h>

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ATT_DISPLAY_NET_OFF = 0,
    ATT_DISPLAY_NET_STARTING,
    ATT_DISPLAY_NET_READY,
    ATT_DISPLAY_NET_ONLINE,
    ATT_DISPLAY_NET_UPLOAD,
    ATT_DISPLAY_NET_ERROR,
} att_display_network_state_t;

att_status_t att_display_init(void);
void att_display_set_config(const att_device_config_t *config);
void att_display_set_record_count(uint32_t count);
void att_display_set_network(att_display_network_state_t state);
void att_display_set_weather(const char *text);
void att_display_show_ready(uint32_t now_sec);
void att_display_show_attendance_ok(uint32_t seq, uint32_t sid, uint32_t now_sec);
void att_display_show_attendance_duplicate(uint32_t now_sec);
void att_display_show_attendance_invalid(uint32_t now_sec);
void att_display_show_error(const char *reason, uint32_t now_sec);
void att_display_show_oled_test(void);
void att_display_poll(uint32_t now_sec);

#ifdef __cplusplus
}
#endif

#endif
