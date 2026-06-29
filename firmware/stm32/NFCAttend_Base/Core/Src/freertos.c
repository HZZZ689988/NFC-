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

#define SAVE_ADDR  0  /* W25Q128 中保存 LED 状态的地址 */
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
static void LED_SaveState(uint8_t state);
static uint8_t LED_RestoreState(void);
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

/* ========== LED 状态存储函数 ========== */

/**
  * @brief  将 LED 状态保存到 W25Q128
  * @param  state: 要保存的状态字
  */
static void LED_SaveState(uint8_t state)
{
  /* 擦除扇区 (4KB, 地址 0 所在的扇区) */
  W25QXX_Erase_Sector(SAVE_ADDR);
  W25QXX_Wait_Busy();

  /* 写入状态数据 */
  W25QXX_Write_NoCheck(&state, SAVE_ADDR, 1);
  W25QXX_Wait_Busy();

  printf("LED state saved: 0x%02X\r\n", state);
}

/**
  * @brief  从 W25Q128 恢复 LED 状态
  * @retval 保存的状态字, 若无有效数据则返回 0
  */
static uint8_t LED_RestoreState(void)
{
  uint8_t state = 0;

  /* 读取 W25Q128 ID 检查芯片是否正常 */
  uint16_t id = W25QXX_ReadID();
  printf("W25Q128 ID: 0x%04X\r\n", id);

  /* 读取保存的状态 */
  W25QXX_Read(&state, SAVE_ADDR, 1);
  printf("Restored LED state: 0x%02X\r\n", state);

  return state;
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

  /* 从 W25Q128 恢复上次保存的 LED 状态 */
  ledState = LED_RestoreState();
  LED_SetLeds(ledState);

  printf("W25Q128 Demo Started\r\n");
  printf("K1=Toggle next  K4=Toggle prev  K3=Save  K6=Restore\r\n");

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

    /* K3: 保存当前 LED 状态到 W25Q128 */
    if (Key_IsShortPressed(KEY_K3))
    {
      LED_SaveState(ledState);
    }

    /* K6: 恢复上次保存的 LED 状态 */
    if (Key_IsShortPressed(KEY_K6))
    {
      ledState = LED_RestoreState();
      LED_SetLeds(ledState);
      printf("K6: State restored, mask=0x%02X\r\n", ledState);
    }

    osDelay(KEY_SCAN_INTERVAL_MS);  /* 10ms 扫描周期 */
  }
  /* USER CODE END StartLedTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
