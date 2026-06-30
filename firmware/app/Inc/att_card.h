#ifndef ATT_CARD_H
#define ATT_CARD_H

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

att_status_t att_card_init(void);
att_status_t att_card_read_uid(att_uid_t *uid);
att_status_t att_card_read_person(att_person_t *person);
att_status_t att_card_issue_checked(const att_person_t *person);
att_status_t att_card_clear_checked(const att_uid_t *expected_uid);

#ifdef __cplusplus
}
#endif

#endif
