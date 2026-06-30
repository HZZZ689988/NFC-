#include "att_card.h"

#include <string.h>

#include "att_crc16.h"
#include "rc522.h"

#define ATT_CARD_ACCOUNT_SECTOR      0u
#define ATT_CARD_ACCOUNT_BLOCK       1u
#define ATT_CARD_ACCOUNT_CRC_OFFSET  14u

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

att_status_t att_card_init(void)
{
    RC522_Platform_Init();
    return ATT_OK;
}

att_status_t att_card_read_uid(att_uid_t *uid)
{
    if (uid == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    if (RC522_ScanCard(uid->bytes) != RC522_OK) {
        return ATT_ERR_NO_CARD;
    }
    return ATT_OK;
}

att_status_t att_card_read_person(att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    att_uid_t current_uid;
    att_status_t status = att_card_read_uid(&current_uid);
    if (status != ATT_OK) {
        return status;
    }

    uint8_t block[16] = {0};
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

att_status_t att_card_issue_checked(const att_person_t *person)
{
    if (person == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    att_uid_t current_uid;
    att_status_t status = att_card_read_uid(&current_uid);
    if (status != ATT_OK) {
        return status;
    }

    if (memcmp(current_uid.bytes, person->uid.bytes, ATT_UID_LEN) != 0) {
        return ATT_ERR_CID_MISMATCH;
    }

    uint8_t block[16] = {0};
    memcpy(block, person->uid.bytes, ATT_UID_LEN);
    put_u32_be(&block[4], person->sid);
    put_u32_be(&block[8], person->points);
    block[12] = (uint8_t)person->card_type;
    uint16_t crc = att_crc16_ccitt_false(block, ATT_CARD_ACCOUNT_CRC_OFFSET);
    block[14] = (uint8_t)(crc >> 8);
    block[15] = (uint8_t)crc;

    return RC522_WriteBlock(ATT_CARD_ACCOUNT_SECTOR, ATT_CARD_ACCOUNT_BLOCK, block) == RC522_OK ? ATT_OK : ATT_ERR;
}

att_status_t att_card_clear_checked(const att_uid_t *expected_uid)
{
    if (expected_uid == NULL) {
        return ATT_ERR_INVALID_ARG;
    }

    att_uid_t current_uid;
    att_status_t status = att_card_read_uid(&current_uid);
    if (status != ATT_OK) {
        return status;
    }

    if (memcmp(current_uid.bytes, expected_uid->bytes, ATT_UID_LEN) != 0) {
        return ATT_ERR_CID_MISMATCH;
    }

    uint8_t zero[16] = {0};
    return RC522_WriteBlock(ATT_CARD_ACCOUNT_SECTOR, ATT_CARD_ACCOUNT_BLOCK, zero) == RC522_OK ? ATT_OK : ATT_ERR;
}
