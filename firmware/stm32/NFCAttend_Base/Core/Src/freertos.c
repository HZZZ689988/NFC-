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
#include "att_storage.h"
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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 串口驱动实例 (用于 printf 调试输出) */
static UartDrv_t g_uart1Drv;

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

/* LED 状态位: bit0~bit6 对应 L1~L7, 1=亮, 0=灭 */
static uint8_t ledState = 0;
/* 当前选中的 LED 索引 (0~6) */
static uint8_t currentLed = 0;
/* USER CODE END Variables */

/* Definitions for ledTask */
osThreadId_t ledTaskHandle;
const osThreadAttr_t ledTask_attributes = {
  .name = "ledTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void AttendanceStorage_Bootstrap(void);
static void AttendanceStorage_PrintStatus(void);
/* USER CODE END FunctionPrototypes */

void StartLedTask(void *argument);

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
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of ledTask */
  ledTaskHandle = osThreadNew(StartLedTask, NULL, &ledTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

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
  UartDrv_Init(&g_uart1Drv, &huart1);
  UartDrv_SetDebugPort(&g_uart1Drv);

  /* 初始化按键驱动 */
  Key_Init(keyConfigs, 6);

  /* 初始化 LED 驱动 (初始化后全灭) */
  LED_Init(ledConfigs, 7);
  LED_SetLeds(0x00);

  /* 初始化 W25Q128 */
  W25QXX_Init();
  AttendanceStorage_Bootstrap();
  ledState = 0;
  LED_SetLeds(ledState);

  printf("NFC Attendance Storage Demo Started\r\n");
  printf("K1=Toggle next  K4=Toggle prev  K3/K6=storage status\r\n");

  /* Infinite loop */
  for(;;)
  {
    /* 按键扫描 (每 10ms 调用一次) */
    Key_Scan();

    /* K1: 切换当前 LED 的亮灭状态, 然后移到下一个 LED */
    if (Key_IsShortPressed(KEY_K1) || Key_IsRepeat(KEY_K1))
    {
      ledState ^= (1 << currentLed);              /* 切换当前 LED */
      LED_SetLeds(ledState);
      currentLed = (currentLed + 1) % 7;          /* 移到下一个 */
      printf("K1: Toggle LED%d, state=0x%02X\r\n", currentLed + 1, ledState);
    }

    /* K4: 切换上一个 LED 的亮灭状态 (反向调节) */
    if (Key_IsShortPressed(KEY_K4) || Key_IsRepeat(KEY_K4))
    {
      currentLed = (currentLed == 0) ? 6 : (currentLed - 1);
      ledState ^= (1 << currentLed);              /* 切换当前 LED */
      LED_SetLeds(ledState);
      printf("K4: Toggle LED%d, state=0x%02X\r\n", currentLed + 1, ledState);
    }

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

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
