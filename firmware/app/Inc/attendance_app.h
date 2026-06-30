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

att_status_t attendance_app_init(void);
void attendance_app_set_serial_send(att_protocol_send_fn send, void *ctx);
void attendance_app_set_time_source(attendance_app_time_fn now, void *ctx);
void attendance_app_dispatch_serial_bytes(const uint8_t *data, size_t len);
void attendance_app_poll_nfc(void);
void attendance_app_poll_serial(void);
void attendance_app_poll_network(void);
void attendance_app_mark_network_ready(void);

#ifdef __cplusplus
}
#endif

#endif
