#include "att_card.h"

#include <string.h>

#include "att_crc16.h"
#include "board_spi_bus.h"
#include "rc522.h"

#define ATT_CARD_ACCOUNT_SECTOR      0u
#define ATT_CARD_ACCOUNT_BLOCK       1u
#define ATT_CARD_ACCOUNT_CRC_OFFSET  14u
#define ATT_CARD_PORTRAIT_BLOCKS     24u
#define ATT_CARD_TEXT_BLOCKS         10u
#define ATT_CARD_TEXT_TOTAL_BLOCKS   (ATT_CARD_TEXT_BLOCKS * 2u)
#define ATT_CARD_PORTRAIT_DONE_MASK  ((1UL << ATT_CARD_PORTRAIT_BLOCKS) - 1UL)
#define ATT_CARD_TEXT_DONE_MASK      ((uint16_t)((1u << ATT_CARD_TEXT_BLOCKS) - 1u))
#define ATT_CARD_SPI_RW_TEST_VALUE   0x7Au

static att_uid_t s_image_uid;
static uint8_t s_image_session_active;
static uint32_t s_portrait_mask;
static uint16_t s_name_mask;
static uint16_t s_department_mask;

static void card_lock(void)
{
    BoardSpiBus_Lock();
}

static void card_unlock(void)
{
    BoardSpiBus_Unlock();
}

static void reset_image_session(void)
{
    memset(&s_image_uid, 0, sizeof(s_image_uid));
    s_image_session_active = 0u;
    s_portrait_mask = 0u;
    s_name_mask = 0u;
    s_department_mask = 0u;
}

static void put_u32_be(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

static uint32_t get_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static uint8_t uid_equal(const att_uid_t *left, const att_uid_t *right)
{
    return (uint8_t)(memcmp(left->bytes, right->bytes, ATT_UID_LEN) == 0);
}

static att_status_t auth_sector(uint8_t sector, const att_uid_t *uid)
{
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t block_addr = (uint8_t)(sector * 4u + 3u);
    return RC522_AuthState(RC522_PICC_AUTHENT1A, block_addr, default_key, (uint8_t *)uid->bytes) == RC522_OK
               ? ATT_OK
               : ATT_ERR;
}

static att_status_t image_block_location(att_card_image_area_t area, uint8_t index,
                                         uint8_t *sector, uint8_t *block)
{
    if (sector == NULL || block == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    switch (area) {
    case ATT_CARD_IMAGE_PORTRAIT:
        if (index >= ATT_CARD_PORTRAIT_BLOCKS) {
            return ATT_ERR_INVALID_ARG;
        }
        *sector = (uint8_t)(1u + (index / 3u));
        *block = (uint8_t)(index % 3u);
        return ATT_OK;

    case ATT_CARD_IMAGE_NAME:
    case ATT_CARD_IMAGE_DEPARTMENT: {
        if (index >= ATT_CARD_TEXT_BLOCKS) {
            return ATT_ERR_INVALID_ARG;
        }
        uint8_t slot = (uint8_t)(index + (area == ATT_CARD_IMAGE_DEPARTMENT ? ATT_CARD_TEXT_BLOCKS : 0u));
        if (slot >= ATT_CARD_TEXT_TOTAL_BLOCKS) {
            return ATT_ERR_INVALID_ARG;
        }
        *sector = (uint8_t)(9u + (slot / 3u));
        *block = (uint8_t)(slot % 3u);
        return ATT_OK;
    }

    default:
        return ATT_ERR_INVALID_ARG;
    }
}

static void mark_image_block(att_card_image_area_t area, uint8_t index)
{
    switch (area) {
    case ATT_CARD_IMAGE_PORTRAIT:
        s_portrait_mask |= (uint32_t)(1UL << index);
        break;
    case ATT_CARD_IMAGE_NAME:
        s_name_mask |= (uint16_t)(1u << index);
        break;
    case ATT_CARD_IMAGE_DEPARTMENT:
        s_department_mask |= (uint16_t)(1u << index);
        break;
    default:
        break;
    }
}

static att_status_t ensure_image_session(const att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }
    if (person->card_type != ATT_CARD_IMAGE) {
        return ATT_ERR;
    }
    if (!s_image_session_active) {
        reset_image_session();
        s_image_uid = person->uid;
        s_image_session_active = 1u;
        return ATT_OK;
    }
    return uid_equal(&s_image_uid, &person->uid) ? ATT_OK : ATT_ERR_CID_MISMATCH;
}

att_status_t att_card_init(void)
{
    card_lock();
    RC522_Platform_Init();
    card_unlock();
    return ATT_OK;
}

att_status_t att_card_diag(att_card_diag_t *diag)
{
    if (diag == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    memset(diag, 0, sizeof(*diag));
    card_lock();
    diag->version_raw = RC522_ReadRegister(RC522_REG_VERSION);
    diag->command = RC522_ReadRegister(RC522_REG_COMMAND);
    diag->com_irq = RC522_ReadRegister(RC522_REG_COMIRQ);
    diag->fifo_level = RC522_ReadRegister(RC522_REG_FIFOLEVEL);
    diag->tx_control = RC522_ReadRegister(RC522_REG_TXCONTROL);
    diag->error = RC522_ReadRegister(RC522_REG_ERROR);
    diag->pins = RC522_Platform_ReadPins();
    diag->shared_mosi_flash_cs = RC522_Platform_MosiSharesFlashCs();
    diag->serial_speed_before = RC522_ReadRegister(RC522_REG_SERIALSPEED);
    RC522_WriteRegister(RC522_REG_SERIALSPEED, ATT_CARD_SPI_RW_TEST_VALUE);
    diag->serial_speed_test = RC522_ReadRegister(RC522_REG_SERIALSPEED);
    RC522_WriteRegister(RC522_REG_SERIALSPEED, diag->serial_speed_before);
    diag->serial_speed_after = RC522_ReadRegister(RC522_REG_SERIALSPEED);
    diag->spi_rw_ok = (uint8_t)((diag->serial_speed_test == ATT_CARD_SPI_RW_TEST_VALUE) &&
                                (diag->serial_speed_after == diag->serial_speed_before));
    RC522_ConfigISOType('A');
    diag->version = RC522_ReadRegister(RC522_REG_VERSION);
    diag->command = RC522_ReadRegister(RC522_REG_COMMAND);
    diag->com_irq = RC522_ReadRegister(RC522_REG_COMIRQ);
    diag->fifo_level = RC522_ReadRegister(RC522_REG_FIFOLEVEL);
    diag->tx_control = RC522_ReadRegister(RC522_REG_TXCONTROL);
    diag->error = RC522_ReadRegister(RC522_REG_ERROR);
    diag->request_status = (int8_t)RC522_Request(RC522_PICC_REQALL, diag->tag_type);
    card_unlock();
    return ATT_OK;
}

static att_status_t read_uid_selected(att_uid_t *uid)
{
    if (uid == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    if (RC522_ScanCard(uid->bytes) != RC522_OK) {
        return ATT_ERR_NO_CARD;
    }
    return ATT_OK;
}

att_status_t att_card_read_uid(att_uid_t *uid)
{
    card_lock();
    att_status_t status = read_uid_selected(uid);
    if (status != ATT_ERR_NO_CARD) {
        RC522_Halt();
    }
    card_unlock();
    return status;
}

static att_status_t read_person_selected(att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    att_uid_t current_uid;
    att_status_t status = read_uid_selected(&current_uid);
    if (status != ATT_OK) {
        return status;
    }

    uint8_t block[16] = {0};
    if (auth_sector(ATT_CARD_ACCOUNT_SECTOR, &current_uid) != ATT_OK) {
        return ATT_ERR;
    }
    if (RC522_ReadBlock(ATT_CARD_ACCOUNT_SECTOR, ATT_CARD_ACCOUNT_BLOCK, block) != RC522_OK) {
        return ATT_ERR;
    }

    if (memcmp(block, current_uid.bytes, ATT_UID_LEN) != 0) {
        return ATT_ERR_CID_MISMATCH;
    }

    uint16_t expected_crc = (uint16_t)(((uint16_t)block[14] << 8) | block[15]);
    uint16_t actual_crc = att_crc16_ccitt_false(block, ATT_CARD_ACCOUNT_CRC_OFFSET);
    if (expected_crc != actual_crc) {
        return ATT_ERR_CRC;
    }

    memset(person, 0, sizeof(*person));
    memcpy(person->uid.bytes, block, ATT_UID_LEN);
    person->sid = get_u32_be(&block[4]);
    person->points = get_u32_be(&block[8]);
    person->card_type = (att_card_type_t)block[12];
    person->crc16 = expected_crc;
    return ATT_OK;
}

att_status_t att_card_read_person(att_person_t *person)
{
    card_lock();
    att_status_t status = read_person_selected(person);
    if (status != ATT_ERR_NO_CARD) {
        RC522_Halt();
    }
    card_unlock();
    return status;
}

att_status_t att_card_issue_checked(const att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    card_lock();
    att_uid_t current_uid;
    att_status_t status = read_uid_selected(&current_uid);
    if (status != ATT_OK) {
        card_unlock();
        return status;
    }

    if (memcmp(current_uid.bytes, person->uid.bytes, ATT_UID_LEN) != 0) {
        RC522_Halt();
        card_unlock();
        return ATT_ERR_CID_MISMATCH;
    }

    if (auth_sector(ATT_CARD_ACCOUNT_SECTOR, &current_uid) != ATT_OK) {
        RC522_Halt();
        card_unlock();
        return ATT_ERR;
    }

    uint8_t block[16] = {0};
    memcpy(block, person->uid.bytes, ATT_UID_LEN);
    put_u32_be(&block[4], person->sid);
    put_u32_be(&block[8], person->points);
    block[12] = (uint8_t)person->card_type;
    uint16_t crc = att_crc16_ccitt_false(block, ATT_CARD_ACCOUNT_CRC_OFFSET);
    block[14] = (uint8_t)(crc >> 8);
    block[15] = (uint8_t)crc;

    att_status_t write_status =
        RC522_WriteBlock(ATT_CARD_ACCOUNT_SECTOR, ATT_CARD_ACCOUNT_BLOCK, block) == RC522_OK ? ATT_OK : ATT_ERR;
    RC522_Halt();
    if (write_status == ATT_OK) {
        reset_image_session();
    }
    card_unlock();
    return write_status;
}

att_status_t att_card_clear_checked(const att_uid_t *expected_uid)
{
    if (expected_uid == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    card_lock();
    att_uid_t current_uid;
    att_status_t status = read_uid_selected(&current_uid);
    if (status != ATT_OK) {
        card_unlock();
        return status;
    }

    if (memcmp(current_uid.bytes, expected_uid->bytes, ATT_UID_LEN) != 0) {
        RC522_Halt();
        card_unlock();
        return ATT_ERR_CID_MISMATCH;
    }

    if (auth_sector(ATT_CARD_ACCOUNT_SECTOR, &current_uid) != ATT_OK) {
        RC522_Halt();
        card_unlock();
        return ATT_ERR;
    }

    uint8_t zero[16] = {0};
    att_status_t write_status =
        RC522_WriteBlock(ATT_CARD_ACCOUNT_SECTOR, ATT_CARD_ACCOUNT_BLOCK, zero) == RC522_OK ? ATT_OK : ATT_ERR;
    RC522_Halt();
    if (write_status == ATT_OK) {
        reset_image_session();
    }
    card_unlock();
    return write_status;
}

att_status_t att_card_write_image_block(att_card_image_area_t area, uint8_t index, const uint8_t data[16])
{
    if (data == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    card_lock();
    uint8_t sector = 0u;
    uint8_t block = 0u;
    att_status_t status = image_block_location(area, index, &sector, &block);
    if (status != ATT_OK) {
        card_unlock();
        return status;
    }

    att_person_t person;
    status = read_person_selected(&person);
    if (status != ATT_OK) {
        if (status != ATT_ERR_NO_CARD) {
            RC522_Halt();
        }
        card_unlock();
        return status;
    }
    status = ensure_image_session(&person);
    if (status != ATT_OK) {
        RC522_Halt();
        card_unlock();
        return status;
    }

    status = auth_sector(sector, &person.uid);
    if (status != ATT_OK) {
        RC522_Halt();
        card_unlock();
        return status;
    }

    uint8_t block_data[16];
    memcpy(block_data, data, sizeof(block_data));
    if (RC522_WriteBlock(sector, block, block_data) != RC522_OK) {
        RC522_Halt();
        card_unlock();
        return ATT_ERR;
    }

    RC522_Halt();
    mark_image_block(area, index);
    card_unlock();
    return ATT_OK;
}

att_status_t att_card_finish_image_update(void)
{
    if (!s_image_session_active) {
        return ATT_ERR_NOT_READY;
    }

    card_lock();
    att_person_t person;
    att_status_t status = read_person_selected(&person);
    if (status != ATT_OK) {
        if (status != ATT_ERR_NO_CARD) {
            RC522_Halt();
        }
        card_unlock();
        return status;
    }
    status = ensure_image_session(&person);
    if (status != ATT_OK) {
        RC522_Halt();
        card_unlock();
        return status;
    }

    if (s_portrait_mask != ATT_CARD_PORTRAIT_DONE_MASK ||
        s_name_mask != ATT_CARD_TEXT_DONE_MASK ||
        s_department_mask != ATT_CARD_TEXT_DONE_MASK) {
        RC522_Halt();
        card_unlock();
        return ATT_ERR_NOT_READY;
    }

    RC522_Halt();
    reset_image_session();
    card_unlock();
    return ATT_OK;
}
