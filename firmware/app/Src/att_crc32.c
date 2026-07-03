#include "att_crc32.h"

uint32_t att_crc32_init(void)
{
    return 0xFFFFFFFFu;
}

uint32_t att_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (bytes == NULL && len != 0u) {
        return crc;
    }

    for (size_t i = 0u; i < len; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t att_crc32_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t att_crc32_ieee(const void *data, size_t len)
{
    return att_crc32_finish(att_crc32_update(att_crc32_init(), data, len));
}
