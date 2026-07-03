#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_crc32.h"
#include "att_storage.h"
#include "lfs.h"

#define TEST_BLOCK_SIZE  256u
#define TEST_BLOCK_COUNT 64u
#define TEST_CACHE_SIZE  64u

static unsigned char g_flash[TEST_BLOCK_SIZE * TEST_BLOCK_COUNT];
static unsigned char g_read_buffer[TEST_CACHE_SIZE];
static unsigned char g_prog_buffer[TEST_CACHE_SIZE];
static unsigned char g_lookahead_buffer[TEST_CACHE_SIZE];

static void require_int(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static int test_read(const struct lfs_config *cfg, lfs_block_t block,
                     lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)cfg;
    memcpy(buffer, &g_flash[(block * TEST_BLOCK_SIZE) + off], size);
    return 0;
}

static int test_prog(const struct lfs_config *cfg, lfs_block_t block,
                     lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)cfg;
    const unsigned char *src = (const unsigned char *)buffer;
    unsigned char *dest = &g_flash[(block * TEST_BLOCK_SIZE) + off];
    for (lfs_size_t i = 0u; i < size; ++i) {
        dest[i] &= src[i];
    }
    return 0;
}

static int test_erase(const struct lfs_config *cfg, lfs_block_t block)
{
    (void)cfg;
    memset(&g_flash[block * TEST_BLOCK_SIZE], 0xFF, TEST_BLOCK_SIZE);
    return 0;
}

static int test_sync(const struct lfs_config *cfg)
{
    (void)cfg;
    return 0;
}

const struct lfs_config g_att_lfs_cfg = {
    .read = test_read,
    .prog = test_prog,
    .erase = test_erase,
    .sync = test_sync,
    .read_size = 16,
    .prog_size = 16,
    .block_size = TEST_BLOCK_SIZE,
    .block_count = TEST_BLOCK_COUNT,
    .block_cycles = 100,
    .cache_size = TEST_CACHE_SIZE,
    .lookahead_size = TEST_CACHE_SIZE,
    .read_buffer = g_read_buffer,
    .prog_buffer = g_prog_buffer,
    .lookahead_buffer = g_lookahead_buffer,
};

static void make_record(att_record_t *record, uint32_t seq)
{
    memset(record, 0, sizeof(*record));
    record->seq = seq;
    record->uid.bytes[0] = (uint8_t)seq;
    record->uid.bytes[1] = 0xA1u;
    record->uid.bytes[2] = 0xB2u;
    record->uid.bytes[3] = 0xC3u;
    record->sid = 1000u + seq;
    record->type = seq & 1u ? ATT_RECORD_IN : ATT_RECORD_OUT;
    record->timestamp = 10000u + seq;
    record->device_id = 7u;
    record->upload_state = ATT_UPLOAD_PENDING;
}

static void test_config_roundtrip(void)
{
    require_int(att_storage_init() == ATT_OK, "storage init should succeed");

    att_device_config_t config;
    require_int(att_storage_load_config(&config) == ATT_OK, "default config should load");
    config.device_id = 77u;
    config.work_mode = ATT_MODE_CHECK_OUT;
    config.timezone = 8;
    require_int(att_storage_save_config(&config) == ATT_OK, "config save should succeed");

    att_device_config_t loaded;
    require_int(att_storage_load_config(&loaded) == ATT_OK, "saved config should load");
    require_int(loaded.device_id == 77u, "device id should persist");
    require_int(loaded.work_mode == ATT_MODE_CHECK_OUT, "work mode should persist");
    require_int(loaded.timezone == 8, "timezone should persist");
}

static void test_record_ring_overwrites_oldest(void)
{
    for (uint32_t seq = 1u; seq <= 6u; ++seq) {
        att_record_t record;
        make_record(&record, seq);
        require_int(att_storage_append_record(&record) == ATT_OK, "append should succeed");
    }

    uint32_t count = 0u;
    require_int(att_storage_record_count(&count) == ATT_OK, "record count should load");
    require_int(count == 4u, "ring should keep max records");

    att_record_t record;
    require_int(att_storage_read_record(0u, &record) == ATT_OK, "oldest logical record should read");
    require_int(record.seq == 3u, "oldest retained record should be seq 3");
    require_int(att_storage_read_record(3u, &record) == ATT_OK, "newest logical record should read");
    require_int(record.seq == 6u, "newest retained record should be seq 6");
}

static void test_pending_upload_marks_in_ring_order(void)
{
    att_record_t record;
    require_int(att_storage_next_pending_upload(&record) == ATT_OK, "first pending should exist");
    require_int(record.seq == 3u, "first pending should be oldest retained");
    require_int(att_storage_mark_uploaded(3u) == ATT_OK, "mark uploaded should succeed");
    require_int(att_storage_next_pending_upload(&record) == ATT_OK, "next pending should exist");
    require_int(record.seq == 4u, "next pending should advance after mark");
}

static void test_mark_uploaded_marks_duplicate_sequences(void)
{
    att_record_t duplicate;
    make_record(&duplicate, 4u);
    duplicate.timestamp = 20004u;
    require_int(att_storage_append_record(&duplicate) == ATT_OK,
                "duplicate sequence append should succeed");
    require_int(att_storage_mark_uploaded(4u) == ATT_OK,
                "duplicate sequence mark should succeed");

    att_record_t record;
    require_int(att_storage_next_pending_upload(&record) == ATT_OK,
                "next pending should exist after duplicate mark");
    require_int(record.seq == 5u,
                "all duplicate sequence records should be marked uploaded");
}

static void test_ota_cache_roundtrip_and_crc(void)
{
    const uint8_t image[] = {0x10u, 0x20u, 0x30u, 0x40u, 0xA5u};
    uint32_t expected_crc32 = att_crc32_ieee(image, sizeof(image));

    require_int(att_storage_ota_begin("v20260703", sizeof(image), expected_crc32) == ATT_OK,
                "OTA begin should create image and metadata");
    require_int(att_storage_ota_write(0u, image, 2u) == ATT_OK,
                "first OTA write should succeed");
    require_int(att_storage_ota_write(2u, image + 2u, sizeof(image) - 2u) == ATT_OK,
                "second OTA write should succeed");

    att_ota_file_status_t status;
    require_int(att_storage_ota_status(&status) == ATT_OK, "OTA status should load");
    require_int(status.valid == 1u, "OTA status should be valid");
    require_int(status.verified == 0u, "OTA should not be verified before CRC pass");
    require_int(strcmp(status.version, "v20260703") == 0, "OTA version should persist");
    require_int(status.size_bytes == sizeof(image), "OTA size should persist");
    require_int(status.received_bytes == sizeof(image), "OTA received bytes should persist");
    require_int(status.expected_crc32 == expected_crc32, "OTA expected CRC should persist");
    require_int(status.target_addr == ATT_APP_SLOT_BASE_ADDR, "OTA target address should persist");
    require_int(status.install_state == ATT_OTA_INSTALL_NONE, "OTA should not be pending before CRC pass");
    require_int(status.install_error == ATT_OTA_INSTALL_ERR_NONE, "OTA install error should be clear before CRC pass");

    uint32_t actual_crc32 = 0u;
    require_int(att_storage_ota_verify(&actual_crc32) == ATT_OK,
                "OTA verify should scan cached image");
    require_int(actual_crc32 == expected_crc32, "OTA verify should produce expected CRC");
    require_int(att_storage_ota_status(&status) == ATT_OK, "verified OTA status should load");
    require_int(status.verified == 1u, "OTA status should be marked verified");
    require_int(status.actual_crc32 == expected_crc32, "OTA actual CRC should persist");
    require_int(status.install_state == ATT_OTA_INSTALL_PENDING,
                "verified OTA should be pending for bootloader install");
    require_int(status.install_error == ATT_OTA_INSTALL_ERR_NONE,
                "verified OTA should have no install error");
}

static void test_ota_rejects_image_larger_than_target_slot(void)
{
    require_int(att_storage_ota_begin("too-large",
                                      ATT_OTA_TARGET_SIZE_BYTES + 1u,
                                      0x12345678u) == ATT_ERR_INVALID_ARG,
                "OTA begin should reject image larger than the target slot");
}

static void test_boot_state_roundtrip(void)
{
    att_boot_state_t state;
    require_int(att_storage_boot_state_load(&state) == ATT_OK,
                "boot state should load or initialize");
    state.active_slot = 2u;
    state.pending_slot = 2u;
    state.confirmed_slot = 1u;
    state.boot_count = 2u;
    state.last_error = ATT_OTA_INSTALL_ERR_VERIFY;
    require_int(att_storage_boot_state_save(&state) == ATT_OK,
                "boot state save should succeed");

    att_boot_state_t loaded;
    require_int(att_storage_boot_state_load(&loaded) == ATT_OK,
                "saved boot state should load");
    require_int(loaded.active_slot == 2u, "active slot should persist");
    require_int(loaded.pending_slot == 2u, "pending slot should persist");
    require_int(loaded.confirmed_slot == 1u, "confirmed slot should persist");
    require_int(loaded.boot_count == 2u, "boot count should persist");
    require_int(loaded.last_error == ATT_OTA_INSTALL_ERR_VERIFY,
                "last boot error should persist");
}

int main(void)
{
    memset(g_flash, 0xFF, sizeof(g_flash));
    test_config_roundtrip();
    test_record_ring_overwrites_oldest();
    test_pending_upload_marks_in_ring_order();
    test_mark_uploaded_marks_duplicate_sequences();
    test_ota_cache_roundtrip_and_crc();
    test_ota_rejects_image_larger_than_target_slot();
    test_boot_state_roundtrip();
    return 0;
}
