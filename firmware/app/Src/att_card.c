#include "att_card.h"

#include <string.h>

#include "att_crc16.h"
#include "rc522.h"

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
    block[4] = (uint8_t)(person->sid >> 24);
    block[5] = (uint8_t)(person->sid >> 16);
    block[6] = (uint8_t)(person->sid >> 8);
    block[7] = (uint8_t)(person->sid);
    block[8] = (uint8_t)person->card_type;
    uint16_t crc = att_crc16_ccitt_false(block, 14);
    block[14] = (uint8_t)(crc >> 8);
    block[15] = (uint8_t)crc;

    return RC522_WriteBlock(1, 0, block) == RC522_OK ? ATT_OK : ATT_ERR;
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
    return RC522_WriteBlock(1, 0, zero) == RC522_OK ? ATT_OK : ATT_ERR;
}
