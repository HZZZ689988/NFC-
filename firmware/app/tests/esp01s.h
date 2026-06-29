#ifndef TEST_ESP01S_H
#define TEST_ESP01S_H

#include <stdint.h>

typedef struct {
    char ssid[32];
    char password[64];
    char tcpServerIP[48];
    uint16_t tcpPort;
    char ntpServer[48];
    int8_t ntpTimezone;
} ESP01S_Config_t;

typedef void (*ESP01S_DataCallback_t)(const uint8_t *pData, uint16_t len, void *pUserCtx);

void ESP01S_SetConfig(const ESP01S_Config_t *pConfig);
void ESP01S_SendStr(const char *str);
void ESP01S_RegisterDataCb(ESP01S_DataCallback_t pCb, void *pUserCtx);
void ESP01S_SyncNtpTime(void);
uint8_t ESP01S_IsNtpSynced(void);
int ESP01S_QueryWeather(const char *apiKey, const char *location,
                        const char *language, const char *unit,
                        char *outCity, uint16_t cityBufSize,
                        char *outTextDay, uint16_t textDayBufSize,
                        char *outHigh, uint16_t highBufSize,
                        char *outTextNight, uint16_t textNightBufSize,
                        char *outLow, uint16_t lowBufSize,
                        char *outPrecip, uint16_t precipBufSize);

#endif
