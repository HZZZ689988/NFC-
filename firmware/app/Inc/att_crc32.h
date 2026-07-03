#ifndef ATT_CRC32_H
#define ATT_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t att_crc32_init(void);
uint32_t att_crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t att_crc32_finish(uint32_t crc);
uint32_t att_crc32_ieee(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
