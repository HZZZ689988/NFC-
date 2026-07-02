/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "w25qxx.h"
#include "key.h"
#include "led.h"
#include "uart_drv.h"
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "attendance_app.h"
#include "att_display.h"
#include "att_network.h"
#include "att_storage.h"
#include "bsp_rtc.h"
#include "esp01s.h"
#include "midi.h"
#include "rtc.h"
#include "tim.h"
#include "usart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 按键索引定义 */
#define KEY_K1  0
#define KEY_K2  1
#define KEY_K3  2
#define KEY_K4  3
#define KEY_K5  4
#define KEY_K6  5

#define ATT_SERIAL_RX_QUEUE_DEPTH 4u
#define ATT_NETWORK_RX_QUEUE_DEPTH 4u
#define ATT_FEEDBACK_QUEUE_DEPTH 8u
#define ATT_NFC_POLL_INTERVAL_MS 500u
#define ATT_NETWORK_START_RETRY_MS 30000u
#define ATT_NETWORK_POLL_INTERVAL_MS 1000u
#define ATT_DISPLAY_POLL_INTERVAL_MS 500u
#define ATT_FEEDBACK_LED_HOLD_MS 800u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 串口驱动实例 (用于 printf 调试输出) */
extern UartDrv_t g_uart1Drv;
extern UartDrv_t g_uart6Drv;
static osMessageQueueId_t serialRxQueueHandle;
static osMessageQueueId_t networkRxQueueHandle;
static osMessageQueueId_t feedbackQueueHandle;
static volatile uint8_t attendanceAppReady;
static uint8_t networkDriverReady;

/* 按键配置: K1~K4 上拉低有效, K5~K6 下拉高有效 */
static const Key_Config_t keyConfigs[6] = {
    { K1_GPIO_Port, K1_Pin, KEY_ACTIVE_LOW  },  /* K1 */
    { K2_GPIO_Port, K2_Pin, KEY_ACTIVE_LOW  },  /* K2 */
    { K3_GPIO_Port, K3_Pin, KEY_ACTIVE_LOW  },  /* K3 */
    { K4_GPIO_Port, K4_Pin, KEY_ACTIVE_LOW  },  /* K4 */
    { K5_GPIO_Port, K5_Pin, KEY_ACTIVE_HIGH },  /* K5 */
    { K6_GPIO_Port, K6_Pin, KEY_ACTIVE_HIGH },  /* K6 */
};

/* LED 配置: L1~L7 低电平点亮 (灌电流) */
static const Led_Config_t ledConfigs[7] = {
    { L1_GPIO_Port, L1_Pin, LED_ON_LOW },  /* L1 */
    { L2_GPIO_Port, L2_Pin, LED_ON_LOW },  /* L2 */
    { L3_GPIO_Port, L3_Pin, LED_ON_LOW },  /* L3 */
    { L4_GPIO_Port, L4_Pin, LED_ON_LOW },  /* L4 */
    { L5_GPIO_Port, L5_Pin, LED_ON_LOW },  /* L5 */
    { L6_GPIO_Port, L6_Pin, LED_ON_LOW },  /* L6 */
    { L7_GPIO_Port, L7_Pin, LED_ON_LOW },  /* L7 */
};

static uint8_t feedbackLedActive;
static uint32_t feedbackLedClearTick;

/* Local LED/buzzer feedback state. */
/* USER CODE END Variables */

/* Definitions for ledTask */
osThreadId_t ledTaskHandle;
const osThreadAttr_t ledTask_attributes = {
  .name = "ledTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for serialTask */
osThreadId_t serialTaskHandle;
const osThreadAttr_t serialTask_attributes = {
  .name = "serialTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for nfcTask */
osThreadId_t nfcTaskHandle;
const osThreadAttr_t nfcTask_attributes = {
  .name = "nfcTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for networkTask */
osThreadId_t networkTaskHandle;
const osThreadAttr_t networkTask_attributes = {
  .name = "networkTask",
  .stack_size = 768 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for displayTask */
osThreadId_t displayTaskHandle;
const osThreadAttr_t displayTask_attributes = {
  .name = "displayTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void AttendanceApp_Bootstrap(void);
static void AttendanceNetwork_InitDriver(void);
static void AttendanceSerial_Send(const char *line, void *ctx);
static void AttendanceFeedback_Send(attendance_feedback_event_t event, void *ctx);
static void AttendanceFeedback_Play(attendance_feedback_event_t event);
static void AttendanceFeedback_ClearExpired(void);
static uint32_t AttendanceTime_Now(void *ctx);
static uint32_t AttendanceTime_DateTimeToUnix(const BSP_RTC_DateTime_t *dt);
static void AttendanceStorage_Bootstrap(void);
static void AttendanceStorage_PrintStatus(void);
/* USER CODE END FunctionPrototypes */

void StartLedTask(void *argument);
void StartSerialTask(void *argument);
void StartNfcTask(void *argument);
void StartNetworkTask(void *argument);
void StartDisplayTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  serialRxQueueHandle = osMessageQueueNew(ATT_SERIAL_RX_QUEUE_DEPTH,
                                          sizeof(UartDrv_QueueEvent_t),
                                          NULL);
  networkRxQueueHandle = osMessageQueueNew(ATT_NETWORK_RX_QUEUE_DEPTH,
                                           sizeof(UartDrv_QueueEvent_t),
                                           NULL);
  feedbackQueueHandle = osMessageQueueNew(ATT_FEEDBACK_QUEUE_DEPTH,
                                          sizeof(attendance_feedback_event_t),
                                          NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of ledTask */
  ledTaskHandle = osThreadNew(StartLedTask, NULL, &ledTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  if (serialRxQueueHandle != NULL)
  {
    serialTaskHandle = osThreadNew(StartSerialTask, NULL, &serialTask_attributes);
  }
  nfcTaskHandle = osThreadNew(StartNfcTask, NULL, &nfcTask_attributes);
  networkTaskHandle = osThreadNew(StartNetworkTask, NULL, &networkTask_attributes);
  displayTaskHandle = osThreadNew(StartDisplayTask, NULL, &displayTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

static void AttendanceApp_Bootstrap(void)
{
  att_status_t status = attendance_app_init();
  if (status != ATT_OK)
  {
    printf("Attendance app init failed: %d\r\n", (int)status);
    return;
  }

  attendanceAppReady = 1u;
  printf("Attendance app ready\r\n");
}

static void AttendanceNetwork_InitDriver(void)
{
  if (networkDriverReady != 0u)
  {
    return;
  }

  if (!g_uart6Drv.initialized)
  {
    UartDrv_Init(&g_uart6Drv, &huart6);
  }

  ESP01S_Init(&g_uart6Drv);
  if (networkRxQueueHandle != NULL)
  {
    ESP01S_RegisterRxQueue(networkRxQueueHandle);
  }
  UartDrv_StartRecv(&g_uart6Drv);
  networkDriverReady = 1u;
}

static void AttendanceSerial_Send(const char *line, void *ctx)
{
  UartDrv_t *drv = (UartDrv_t *)ctx;
  if (line == NULL)
  {
    return;
  }

  if (drv != NULL && drv->initialized)
  {
    UartDrv_SendStr(drv, line);
    return;
  }

  printf("%s", line);
}

static void AttendanceFeedback_Send(attendance_feedback_event_t event, void *ctx)
{
  (void)ctx;
  if (feedbackQueueHandle != NULL)
  {
    (void)osMessageQueuePut(feedbackQueueHandle, &event, 0u, 0u);
  }
}

static void AttendanceFeedback_Play(attendance_feedback_event_t event)
{
  uint8_t led_mask = 0u;
  uint8_t tune = 0u;
  uint16_t duration_ms = 0u;

  switch (event)
  {
    case ATT_FEEDBACK_ATTEND_OK:
      led_mask = (uint8_t)(1u << 0);
      tune = 13u;
      duration_ms = 120u;
      break;
    case ATT_FEEDBACK_CARD_INVALID:
      led_mask = (uint8_t)(1u << 1);
      tune = 6u;
      duration_ms = 220u;
      break;
    case ATT_FEEDBACK_ATTEND_DUPLICATE:
      led_mask = (uint8_t)(1u << 2);
      tune = 9u;
      duration_ms = 80u;
      break;
    case ATT_FEEDBACK_ERROR:
    case ATT_FEEDBACK_NETWORK_FAULT:
      led_mask = (uint8_t)(1u << 3);
      tune = 4u;
      duration_ms = 250u;
      break;
    case ATT_FEEDBACK_NETWORK_ONLINE:
      led_mask = (uint8_t)(1u << 4);
      tune = 15u;
      duration_ms = 80u;
      break;
    default:
      break;
  }

  if (led_mask != 0u)
  {
    LED_SetLeds(led_mask);
    feedbackLedActive = 1u;
    feedbackLedClearTick = osKernelGetTickCount() + ATT_FEEDBACK_LED_HOLD_MS;
  }

  if (duration_ms != 0u)
  {
    MIDI_Beep(tune, duration_ms);
  }
}

static void AttendanceFeedback_ClearExpired(void)
{
  if (feedbackLedActive != 0u &&
      (int32_t)(osKernelGetTickCount() - feedbackLedClearTick) >= 0)
  {
    LED_SetLeds(0u);
    feedbackLedActive = 0u;
  }
}

static uint32_t AttendanceTime_Now(void *ctx)
{
  (void)ctx;
  BSP_RTC_DateTime_t dt;
  if (BSP_RTC_GetDateTime(&dt) == HAL_OK && dt.year > 2020u)
  {
    return AttendanceTime_DateTimeToUnix(&dt);
  }

  return (uint32_t)(osKernelGetTickCount() / 1000u);
}

static uint32_t AttendanceTime_DateTimeToUnix(const BSP_RTC_DateTime_t *dt)
{
  static const uint16_t daysBeforeMonth[12] = {
      0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u
  };
  uint32_t days = 0u;
  uint16_t year;

  if (dt == NULL || dt->year < 1970u || dt->month == 0u || dt->month > 12u || dt->day == 0u)
  {
    return 0u;
  }

  for (year = 1970u; year < dt->year; ++year)
  {
    days += BSP_RTC_IsLeapYear(year) ? 366u : 365u;
  }

  days += daysBeforeMonth[dt->month - 1u];
  if (dt->month > 2u && BSP_RTC_IsLeapYear(dt->year))
  {
    days += 1u;
  }
  days += (uint32_t)(dt->day - 1u);

  return days * 86400u +
         (uint32_t)dt->hour * 3600u +
         (uint32_t)dt->minute * 60u +
         (uint32_t)dt->second;
}

static void AttendanceStorage_Bootstrap(void)
{
  uint16_t id = W25QXX_ReadID();
  printf("W25Q128 ID: 0x%04X\r\n", id);

  att_status_t status = att_storage_init();
  if (status != ATT_OK)
  {
    printf("LittleFS storage init failed: %d\r\n", (int)status);
    return;
  }

  att_device_config_t config;
  status = att_storage_load_config(&config);
  if (status != ATT_OK)
  {
    printf("Device config load failed: %d\r\n", (int)status);
    return;
  }

  printf("LittleFS storage ready\r\n");
  printf("Device config: id=%lu mode=%u upload=%u repeat=%u\r\n",
         (unsigned long)config.device_id,
         (unsigned int)config.work_mode,
         (unsigned int)config.upload_enable,
         (unsigned int)config.repeat_interval_sec);
}

static void AttendanceStorage_PrintStatus(void)
{
  att_status_t status = att_storage_init();
  if (status != ATT_OK)
  {
    printf("Storage status unavailable: %d\r\n", (int)status);
    return;
  }

  att_device_config_t config;
  status = att_storage_load_config(&config);
  if (status != ATT_OK)
  {
    printf("Config status unavailable: %d\r\n", (int)status);
    return;
  }

  uint32_t count = 0;
  status = att_storage_record_count(&count);
  if (status != ATT_OK)
  {
    printf("Record count unavailable: %d\r\n", (int)status);
    return;
  }

  printf("Storage status: records=%lu device=%lu upload=%u\r\n",
         (unsigned long)count,
         (unsigned long)config.device_id,
         (unsigned int)config.upload_enable);
}
/* USER CODE BEGIN Header_StartLedTask */
/**
  * @brief  W25Q128 存储 LED 状态示例任务
  *         - K1: 切换当前 LED 的亮灭状态, 移到下一个
  *         - K4: 切换上一个 LED 的亮灭状态 (反向调节)
  *         - K3: 保存当前 LED 状态到 W25Q128
  *         - K6: 恢复上次保存的 LED 状态 (从 W25Q128 读取)
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartLedTask */
void StartLedTask(void *argument)
{
  /* USER CODE BEGIN StartLedTask */
  /* 初始化串口驱动并设置为 printf 调试输出口 */
  if (!g_uart1Drv.initialized)
  {
    UartDrv_Init(&g_uart1Drv, &huart1);
  }
  UartDrv_SetDebugPort(&g_uart1Drv);

  /* 初始化按键驱动 */
  Key_Init(keyConfigs, 6);

  /* 初始化 LED 驱动 (初始化后全灭) */
  LED_Init(ledConfigs, 7);
  LED_SetLeds(0x00);
  MIDI_Init(&htim3, TIM_CHANNEL_1);
  MIDI_SetVolume(30u);

  /* 初始化 W25Q128 */
  W25QXX_Init();
  AttendanceNetwork_InitDriver();
  AttendanceApp_Bootstrap();
  attendance_app_set_serial_send(AttendanceSerial_Send, &g_uart1Drv);
  attendance_app_set_time_source(AttendanceTime_Now, NULL);
  attendance_app_set_feedback(AttendanceFeedback_Send, NULL);
  if (serialRxQueueHandle != NULL)
  {
    UartDrv_RegisterRxQueue(&g_uart1Drv, serialRxQueueHandle);
    UartDrv_StartRecv(&g_uart1Drv);
  }
  feedbackLedActive = 0u;
  LED_SetLeds(0u);

  printf("NFC Attendance app started\r\n");
  printf("K3=status K6=storage bootstrap L1=OK L2=invalid L3=duplicate L4=fault L5=network\r\n");

  /* Infinite loop */
  for(;;)
  {
    /* 按键扫描 (每 10ms 调用一次) */
    Key_Scan();

    if (feedbackQueueHandle != NULL)
    {
      attendance_feedback_event_t event;
      while (osMessageQueueGet(feedbackQueueHandle, &event, NULL, 0u) == osOK)
      {
        AttendanceFeedback_Play(event);
      }
    }

    AttendanceFeedback_ClearExpired();
    MIDI_Tick();

    /* K3: report LittleFS ownership */
    if (Key_IsShortPressed(KEY_K3))
    {
      AttendanceStorage_PrintStatus();
    }

    /* K6: re-run storage bootstrap */
    if (Key_IsShortPressed(KEY_K6))
    {
      AttendanceStorage_Bootstrap();
    }

    osDelay(KEY_SCAN_INTERVAL_MS);  /* 10ms 扫描周期 */
  }
  /* USER CODE END StartLedTask */
}

void StartSerialTask(void *argument)
{
  /* USER CODE BEGIN StartSerialTask */
  (void)argument;

  UartDrv_QueueEvent_t event;
  for (;;)
  {
    if (serialRxQueueHandle == NULL)
    {
      osDelay(1000);
      continue;
    }

    if (osMessageQueueGet(serialRxQueueHandle, &event, NULL, osWaitForever) == osOK)
    {
      attendance_app_dispatch_serial_bytes(event.data, event.len);
    }
  }
  /* USER CODE END StartSerialTask */
}

void StartNfcTask(void *argument)
{
  /* USER CODE BEGIN StartNfcTask */
  (void)argument;

  while (attendanceAppReady == 0u)
  {
    osDelay(100u);
  }

  for (;;)
  {
    attendance_app_poll_nfc();
    osDelay(ATT_NFC_POLL_INTERVAL_MS);
  }
  /* USER CODE END StartNfcTask */
}

void StartNetworkTask(void *argument)
{
  /* USER CODE BEGIN StartNetworkTask */
  (void)argument;

  att_display_set_network(ATT_DISPLAY_NET_STARTING);

  while (attendanceAppReady == 0u)
  {
    osDelay(100u);
  }

  for (;;)
  {
    AttendanceNetwork_InitDriver();
    int start_status = ESP01S_Start();
    if (start_status == 0)
    {
      attendance_app_mark_network_ready();
      if (ESP01S_SetRtcFromNtp(&hrtc) == 0)
      {
        BSP_RTC_MarkInitialized();
        printf("RTC synced from ESP01S NTP\r\n");
      }
      att_display_set_network(ATT_DISPLAY_NET_ONLINE);
      printf("ESP01S network ready\r\n");
      break;
    }

    att_display_set_network(ATT_DISPLAY_NET_ERROR);
    printf("ESP01S network start failed: %d\r\n", start_status);
    osDelay(ATT_NETWORK_START_RETRY_MS);
  }

  for (;;)
  {
    attendance_app_poll_network();
    if (networkRxQueueHandle != NULL)
    {
      UartDrv_QueueEvent_t event;
      while (osMessageQueueGet(networkRxQueueHandle, &event, NULL, 0u) == osOK)
      {
        att_network_handle_rx(event.data, event.len);
      }
    }
    osDelay(ATT_NETWORK_POLL_INTERVAL_MS);
  }
  /* USER CODE END StartNetworkTask */
}

void StartDisplayTask(void *argument)
{
  /* USER CODE BEGIN StartDisplayTask */
  (void)argument;

  att_status_t status = att_display_init();
  if (status != ATT_OK)
  {
    printf("OLED display init failed: %d\r\n", (int)status);
  }

  for (;;)
  {
    att_display_poll(AttendanceTime_Now(NULL));
    osDelay(ATT_DISPLAY_POLL_INTERVAL_MS);
  }
  /* USER CODE END StartDisplayTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
