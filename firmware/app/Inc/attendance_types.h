#ifndef ATTENDANCE_TYPES_H
#define ATTENDANCE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATT_UID_LEN                 4u
#define ATT_UID_HEX_LEN             8u
#define ATT_NAME_MAX_LEN            32u
#define ATT_DEPARTMENT_MAX_LEN      32u
#define ATT_WIFI_SSID_MAX_LEN       32u
#define ATT_WIFI_PASSWORD_MAX_LEN   64u
#define ATT_SERVER_HOST_MAX_LEN     64u
#define ATT_WEATHER_KEY_MAX_LEN     48u
#define ATT_WEATHER_LOCATION_LEN    32u

typedef enum {
    ATT_OK = 0,
    ATT_ERR = -1,
    ATT_ERR_INVALID_ARG = -2,
    ATT_ERR_STORAGE = -3,
    ATT_ERR_CRC = -4,
    ATT_ERR_NO_CARD = -5,
    ATT_ERR_CID_MISMATCH = -6,
    ATT_ERR_NOT_READY = -7,
} att_status_t;

typedef enum {
    ATT_CARD_NORMAL = 0,
    ATT_CARD_IMAGE = 1,
    ATT_CARD_ADMIN = 2,
} att_card_type_t;

typedef enum {
    ATT_MODE_NORMAL = 0,
    ATT_MODE_CHECK_IN = 1,
    ATT_MODE_CHECK_OUT = 2,
    ATT_MODE_IN_OUT = 3,
} att_work_mode_t;

typedef enum {
    ATT_RECORD_IN = 0,
    ATT_RECORD_OUT = 1,
    ATT_RECORD_NORMAL = 2,
} att_record_type_t;

typedef enum {
    ATT_UPLOAD_PENDING = 0,
    ATT_UPLOAD_DONE = 1,
    ATT_UPLOAD_FAILED = 2,
} att_upload_state_t;

typedef struct {
    uint8_t bytes[ATT_UID_LEN];
} att_uid_t;

typedef struct {
    uint32_t device_id;
    att_work_mode_t work_mode;
    uint8_t upload_enable;
    uint16_t repeat_interval_sec;
    char wifi_ssid[ATT_WIFI_SSID_MAX_LEN];
    char wifi_password[ATT_WIFI_PASSWORD_MAX_LEN];
    char server_host[ATT_SERVER_HOST_MAX_LEN];
    uint16_t server_port;
    char weather_key[ATT_WEATHER_KEY_MAX_LEN];
    char weather_location[ATT_WEATHER_LOCATION_LEN];
    int8_t timezone;
} att_device_config_t;

typedef struct {
    att_uid_t uid;
    uint32_t sid;
    uint32_t points;
    att_card_type_t card_type;
    char name[ATT_NAME_MAX_LEN];
    char department[ATT_DEPARTMENT_MAX_LEN];
    uint16_t crc16;
} att_person_t;

typedef struct {
    uint32_t seq;
    att_uid_t uid;
    uint32_t sid;
    att_record_type_t type;
    uint32_t timestamp;
    uint32_t device_id;
    att_upload_state_t upload_state;
    uint16_t crc16;
} att_record_t;

#ifdef __cplusplus
}
#endif

#endif
