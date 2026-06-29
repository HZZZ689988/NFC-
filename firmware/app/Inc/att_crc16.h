#ifndef ATT_CRC16_H
#define ATT_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t att_crc16_ccitt_false(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
