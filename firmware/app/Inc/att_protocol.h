#ifndef ATT_PROTOCOL_H
#define ATT_PROTOCOL_H

#include <stddef.h>

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*att_protocol_send_fn)(const char *line, void *ctx);

att_status_t att_protocol_handle_line(const char *line, att_protocol_send_fn send, void *ctx);
att_status_t att_protocol_build_frame(const char *payload, char *out, size_t out_len);
att_status_t att_protocol_parse_frame(const char *line, char *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif
