#include "attendance_app.h"

#include <string.h>

#include "att_card.h"
#include "att_storage.h"

#ifndef ATT_ENABLE_NETWORK
#define ATT_ENABLE_NETWORK 0
#endif

#if ATT_ENABLE_NETWORK
#include "att_network.h"
#endif

static att_device_config_t s_config;
static uint32_t s_next_seq = 1u;

att_status_t attendance_app_init(void)
{
    att_status_t status = att_storage_init();
    if (status != ATT_OK) {
        return status;
    }

    status = att_storage_load_config(&s_config);
    if (status != ATT_OK) {
        att_storage_default_config(&s_config);
        (void)att_storage_save_config(&s_config);
    }

    uint32_t count = 0u;
    if (att_storage_record_count(&count) == ATT_OK) {
        s_next_seq = count + 1u;
    }

    (void)att_card_init();
#if ATT_ENABLE_NETWORK
    (void)att_network_init(&s_config);
#endif
    return ATT_OK;
}

void attendance_app_poll_nfc(void)
{
    att_uid_t uid;
    if (att_card_read_uid(&uid) != ATT_OK) {
        return;
    }

    att_record_t record;
    memset(&record, 0, sizeof(record));
    record.seq = s_next_seq++;
    record.uid = uid;
    record.type = ATT_RECORD_NORMAL;
    record.device_id = s_config.device_id;
    record.upload_state = ATT_UPLOAD_PENDING;

    (void)att_storage_append_record(&record);
}

void attendance_app_poll_serial(void)
{
    /* Wire this function to UartDrv line callbacks in the STM32 base project. */
}

void attendance_app_poll_network(void)
{
#if ATT_ENABLE_NETWORK
    (void)att_network_upload_pending();
#endif
}
