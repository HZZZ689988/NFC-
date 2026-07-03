#ifndef ATT_OTA_FORMAT_H
#define ATT_OTA_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATT_OTA_IMAGE_PATH          "ota.bin"
#define ATT_OTA_META_PATH           "ota.meta"
#define ATT_OTA_META_MAGIC          0x41544F54u
#define ATT_OTA_META_VERSION        3u

#define ATT_BOOT_STATE_PATH         "bootstate.bin"
#define ATT_BOOT_STATE_MAGIC        0x41544254u
#define ATT_BOOT_STATE_VERSION      1u

#define ATT_FLASH_BASE_ADDR         0x08000000u
#define ATT_FLASH_END_ADDR          0x08080000u
#define ATT_BOOTLOADER_BASE_ADDR    0x08000000u
#define ATT_BOOTLOADER_SIZE_BYTES   0x00010000u
#define ATT_APP_SLOT_A_BASE_ADDR    0x08010000u
#define ATT_APP_SLOT_A_SIZE_BYTES   0x00030000u
#define ATT_APP_SLOT_A_END_ADDR     (ATT_APP_SLOT_A_BASE_ADDR + ATT_APP_SLOT_A_SIZE_BYTES)
#define ATT_APP_SLOT_B_BASE_ADDR    0x08040000u
#define ATT_APP_SLOT_B_SIZE_BYTES   0x00040000u
#define ATT_APP_SLOT_B_END_ADDR     (ATT_APP_SLOT_B_BASE_ADDR + ATT_APP_SLOT_B_SIZE_BYTES)
#define ATT_APP_SLOT_MAX_SIZE_BYTES ATT_APP_SLOT_B_SIZE_BYTES

#ifndef ATT_APP_BASE_ADDRESS
#define ATT_APP_BASE_ADDRESS        ATT_FLASH_BASE_ADDR
#endif

#if ATT_APP_BASE_ADDRESS == ATT_APP_SLOT_B_BASE_ADDR
#define ATT_APP_SLOT_ID             2u
#define ATT_OTA_TARGET_SLOT_ID      1u
#define ATT_OTA_TARGET_BASE_ADDR    ATT_APP_SLOT_A_BASE_ADDR
#define ATT_OTA_TARGET_SIZE_BYTES   ATT_APP_SLOT_A_SIZE_BYTES
#elif ATT_APP_BASE_ADDRESS == ATT_APP_SLOT_A_BASE_ADDR
#define ATT_APP_SLOT_ID             1u
#define ATT_OTA_TARGET_SLOT_ID      2u
#define ATT_OTA_TARGET_BASE_ADDR    ATT_APP_SLOT_B_BASE_ADDR
#define ATT_OTA_TARGET_SIZE_BYTES   ATT_APP_SLOT_B_SIZE_BYTES
#else
#define ATT_APP_SLOT_ID             0u
#define ATT_OTA_TARGET_SLOT_ID      1u
#define ATT_OTA_TARGET_BASE_ADDR    ATT_APP_SLOT_A_BASE_ADDR
#define ATT_OTA_TARGET_SIZE_BYTES   ATT_APP_SLOT_A_SIZE_BYTES
#endif

#define ATT_APP_SLOT_BASE_ADDR      ATT_OTA_TARGET_BASE_ADDR
#define ATT_APP_SLOT_SIZE_BYTES     ATT_OTA_TARGET_SIZE_BYTES
#define ATT_APP_SLOT_END_ADDR       (ATT_APP_SLOT_BASE_ADDR + ATT_APP_SLOT_SIZE_BYTES)

typedef enum {
    ATT_OTA_INSTALL_NONE = 0,
    ATT_OTA_INSTALL_PENDING = 1,
    ATT_OTA_INSTALL_INSTALLING = 2,
    ATT_OTA_INSTALL_INSTALLED = 3,
    ATT_OTA_INSTALL_FAILED = 4,
} att_ota_install_state_t;

typedef enum {
    ATT_OTA_INSTALL_ERR_NONE = 0,
    ATT_OTA_INSTALL_ERR_META = 1,
    ATT_OTA_INSTALL_ERR_TARGET = 2,
    ATT_OTA_INSTALL_ERR_SIZE = 3,
    ATT_OTA_INSTALL_ERR_VECTOR = 4,
    ATT_OTA_INSTALL_ERR_IMAGE_CRC = 5,
    ATT_OTA_INSTALL_ERR_ERASE = 6,
    ATT_OTA_INSTALL_ERR_PROGRAM = 7,
    ATT_OTA_INSTALL_ERR_VERIFY = 8,
} att_ota_install_error_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    char image_version[16];
    uint32_t size_bytes;
    uint32_t received_bytes;
    uint32_t expected_crc32;
    uint32_t actual_crc32;
    uint32_t target_addr;
    uint32_t target_slot;
    uint32_t install_state;
    uint32_t install_error;
    uint8_t verified;
    uint8_t reserved[3];
    uint16_t crc16;
} att_ota_meta_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t confirmed_slot;
    uint32_t boot_count;
    uint32_t last_error;
    uint16_t crc16;
} att_boot_state_t;

#ifdef __cplusplus
}
#endif

#endif
