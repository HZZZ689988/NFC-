#include "att_crc16.h"

uint16_t att_crc16_ccitt_false(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}
