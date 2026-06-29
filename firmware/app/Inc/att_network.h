#ifndef ATT_NETWORK_H
#define ATT_NETWORK_H

#include "attendance_types.h"

#ifdef __cplusplus
extern "C" {
#endif

att_status_t att_network_init(const att_device_config_t *config);
att_status_t att_network_sync_time(void);
att_status_t att_network_query_weather(void);
att_status_t att_network_upload_pending(void);
att_status_t att_network_send_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif
