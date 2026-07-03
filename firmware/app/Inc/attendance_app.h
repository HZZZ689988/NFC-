#ifndef ATTENDANCE_APP_H
#define ATTENDANCE_APP_H

#include <stddef.h>
#include <stdint.h>

#include "att_protocol.h"
#include "attendance_types.h"

#define ATT_SERIAL_LINE_MAX 96u

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*attendance_app_time_fn)(void *ctx);

typedef enum {
    ATT_FEEDBACK_ATTEND_OK = 0,
    ATT_FEEDBACK_ATTEND_DUPLICATE,
    ATT_FEEDBACK_CARD_INVALID,
    ATT_FEEDBACK_ERROR,
    ATT_FEEDBACK_NETWORK_FAULT,
    ATT_FEEDBACK_NETWORK_ONLINE,
} attendance_feedback_event_t;

typedef void (*attendance_feedback_fn)(attendance_feedback_event_t event, void *ctx);
typedef void (*attendance_reset_fn)(void *ctx);

typedef enum {
    ATT_ADMIN_ACTION_PREV_FIELD = 0,
    ATT_ADMIN_ACTION_NEXT_FIELD,
    ATT_ADMIN_ACTION_DEC,
    ATT_ADMIN_ACTION_INC,
    ATT_ADMIN_ACTION_SAVE,
    ATT_ADMIN_ACTION_EXIT,
} attendance_admin_action_t;

typedef enum {
    ATT_ADMIN_RESULT_IGNORED = 0,
    ATT_ADMIN_RESULT_HANDLED,
    ATT_ADMIN_RESULT_SAVED,
} attendance_admin_result_t;

att_status_t attendance_app_init(void);
void attendance_app_set_serial_send(att_protocol_send_fn send, void *ctx);
void attendance_app_set_time_source(attendance_app_time_fn now, void *ctx);
void attendance_app_set_feedback(attendance_feedback_fn feedback, void *ctx);
void attendance_app_set_reset(attendance_reset_fn reset, void *ctx);
int8_t attendance_app_get_timezone(void);
uint8_t attendance_app_admin_is_active(void);
attendance_admin_result_t attendance_app_admin_handle_action(attendance_admin_action_t action);
void attendance_app_dispatch_serial_bytes(const uint8_t *data, size_t len);
void attendance_app_poll_nfc(void);
void attendance_app_poll_serial(void);
void attendance_app_poll_network(void);
void attendance_app_mark_network_ready(void);

#ifdef __cplusplus
}
#endif

#endif
