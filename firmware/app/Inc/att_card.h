#ifndef ATT_CARD_H
#define ATT_CARD_H

#include <stdint.h>

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

att_status_t att_card_init(void);
att_status_t att_card_read_uid(att_uid_t *uid);
att_status_t att_card_read_person(att_person_t *person);
att_status_t att_card_issue_checked(const att_person_t *person);
typedef enum {
    ATT_CARD_IMAGE_PORTRAIT = 0,
    ATT_CARD_IMAGE_NAME = 1,
    ATT_CARD_IMAGE_DEPARTMENT = 2,
} att_card_image_area_t;

att_status_t att_card_clear_checked(const att_uid_t *expected_uid);
att_status_t att_card_write_image_block(att_card_image_area_t area, uint8_t index, const uint8_t data[16]);
att_status_t att_card_finish_image_update(void);

#ifdef __cplusplus
}
#endif

#endif
