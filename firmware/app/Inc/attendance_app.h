#ifndef ATTENDANCE_APP_H
#define ATTENDANCE_APP_H

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

att_status_t attendance_app_init(void);
void attendance_app_poll_nfc(void);
void attendance_app_poll_serial(void);
void attendance_app_poll_network(void);

#ifdef __cplusplus
}
#endif

#endif
