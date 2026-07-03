#ifndef ATT_TEMPERATURE_H
#define ATT_TEMPERATURE_H

#include <stdint.h>

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

att_status_t att_temperature_read_centi_c(int16_t *centi_c);

#ifdef __cplusplus
}
#endif

#endif
