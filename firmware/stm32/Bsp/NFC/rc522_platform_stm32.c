/**
 * @file    rc522_platform_stm32.c
 * @brief   RC522 STM32 平台适配层
 * @details 本文件提供针对 STM32F4 系列 (HAL 库) 的平台 IO 接口实现。
 *          使用 CubeMX 生成的 GPIO 引脚定义，通过软件模拟 SPI 与 RC522 通讯。
 *
 *          引脚连接 (默认，可在 main.h 中修改):
 *          - NFC_NSS(PB13)  -> 片选 (CS/SDA)
 *          - NFC_RST(PA2)   -> 复位 (RST)
 *          - NFC_MOSI(PC4)  -> MOSI
 *          - NFC_MISO(PA1)  -> MISO
 *          - NFC_SCK(PB11)  -> SCK
 *          - GND/+3V3       -> 模块物理供电
 *
 *          使用方法:
 *          1. 在 CubeMX 中配置上述 GPIO 引脚 (输出: NSS, RST, MOSI, SCK; 输入: MISO)
 *          2. 调用 RC522_Platform_Init() 完成初始化
 *          3. 之后即可调用 rc522.h 中的 API 函数
 *
 * @note    编码: UTF-8
 */

#include "rc522.h"
#include "board_spi_bus.h"
#include "delay_us.h"   /* 公共微秒延时服务 */
#include "FreeRTOS.h"   /* FreeRTOS 基础头文件，须在 task.h 之前 */
#include "task.h"       /* taskENTER_CRITICAL / taskEXIT_CRITICAL */
#include "main.h"       /* 包含 NFC_XXX_Pin/GPIO_Port 定义 */

#define RC522_PIN_NSS   0x01u
#define RC522_PIN_SCK   0x02u
#define RC522_PIN_MOSI  0x04u
#define RC522_PIN_MISO  0x08u
#define RC522_PIN_RST   0x10u
#define RC522_PIN_FLASH 0x20u

/* ======================================================
 *  GPIO 控制函数 (RC522_IO_t 回调实现)
 * ====================================================== */

static void rc522_gpio_configure(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    HAL_GPIO_WritePin(NFC_NSS_GPIO_Port, NFC_NSS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin, GPIO_PIN_SET);
#if defined(SPI1_CS_Pin) && defined(SPI1_CS_GPIO_Port)
    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
#endif

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_InitStruct.Pin = NFC_NSS_Pin;
    HAL_GPIO_Init(NFC_NSS_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = NFC_RST_Pin;
    HAL_GPIO_Init(NFC_RST_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = NFC_SCK_Pin;
    HAL_GPIO_Init(NFC_SCK_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = NFC_MOSI_Pin;
    HAL_GPIO_Init(NFC_MOSI_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = NFC_MISO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(NFC_MISO_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief RC522 片选 (CS/NSS) 控制
 * @param level 0=选中 (低电平), 1=释放 (高电平)
 */
static void cs_control(uint8_t level)
{
    if (level) {
        HAL_GPIO_WritePin(NFC_NSS_GPIO_Port, NFC_NSS_Pin, GPIO_PIN_SET);
#if defined(SPI1_CS_Pin) && defined(SPI1_CS_GPIO_Port)
        if ((NFC_MOSI_GPIO_Port == SPI1_CS_GPIO_Port) && (NFC_MOSI_Pin == SPI1_CS_Pin)) {
            HAL_GPIO_WritePin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin, GPIO_PIN_SET);
        }
#endif
        BoardSpiBus_Unlock();
    } else {
        BoardSpiBus_Lock();
        HAL_GPIO_WritePin(NFC_NSS_GPIO_Port, NFC_NSS_Pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief RC522 复位 (RST) 控制
 * @param level 0=复位 (低电平), 1=正常工作 (高电平)
 */
static void rst_control(uint8_t level)
{
    if (level) {
        HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief SPI 全双工字节传输 (软件模拟 SPI Mode 0)
 * @param data 要发送的数据字节
 * @return 接收到的数据字节
 *
 * @note  SPI Mode 0 (CPOL=0, CPHA=0):
 *        - 空闲时 SCK = 低电平
 *        - 数据在 SCK 上升沿发送 (MOSI 更新)
 *        - 数据在 SCK 下降沿接收 (MISO 采样)
 *
 *        时序:
 *        for 8 bits:
 *            SCK = LOW
 *            set MOSI (根据 data 的最高位)
 *            SCK = HIGH
 *            read MISO
 *            data <<= 1
 *            data |= MISO
 *        SCK = LOW (结束状态)
 */
static uint8_t spi_transfer_byte(uint8_t data)
{
    uint8_t rx = 0;
    BaseType_t scheduler_started = (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);

    /* 临界区保护：防止任务切换导致 SCK 时序断裂
     * 一个字节传输约 32 µs，临界区长度可接受 */
    if (scheduler_started) {
        taskENTER_CRITICAL();
    }

    for (int8_t i = 7; i >= 0; i--) {
        /* SCK 下降沿 -> 拉低 SCK */
        HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
        delay_us(1);

        /* 设置 MOSI 数据位 (MSB 优先) */
        if (data & 0x80) {
            HAL_GPIO_WritePin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin, GPIO_PIN_RESET);
        }
        delay_us(1);

        /* SCK 上升沿 -> 从机采样 MOSI */
        HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_SET);
        delay_us(1);

        /* 在 SCK 高电平时采样 MISO */
        rx <<= 1;
        if (HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port, NFC_MISO_Pin) == GPIO_PIN_SET) {
            rx |= 0x01;
        }
        delay_us(1);

        data <<= 1;
    }

    /* 结束状态: SCK 回到低电平 */
    HAL_GPIO_WritePin(NFC_SCK_GPIO_Port, NFC_SCK_Pin, GPIO_PIN_RESET);
    delay_us(1);

    if (scheduler_started) {
        taskEXIT_CRITICAL();
    }

    return rx;
}

/**
 * @brief 毫秒延时 (直接使用 HAL_Delay)
 * @param ms 延时毫秒数
 */
static void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ======================================================
 *  平台 IO 接口实例
 * ====================================================== */

/** RC522 平台 IO 接口，供 RC522_Init() 使用 */
static const RC522_IO_t s_rc522_io = {
    .cs_control   = cs_control,
    .rst_control  = rst_control,
    .spi_transfer = spi_transfer_byte,
    .delay_us     = delay_us,
    .delay_ms     = delay_ms
};

/* ======================================================
 *  公开 API
 * ====================================================== */

/**
 * @brief 初始化 RC522 STM32 平台
 * @note  此函数完成:
 *        1. DWT 定时器初始化 (微秒延时)
 *        2. NFC 模块接地引脚拉低 (如果有定义)
 *        3. 调用 RC522_Init() 初始化 RC522 芯片
 *
 *        在 FreeRTOS 任务中调用此函数前需确保 HAL_Init()
 *        和 MX_GPIO_Init() 等系统初始化已完成。
 */
void RC522_Platform_Init(void)
{
    /* 初始化公共微秒延时服务 */
    delay_us_init();

    rc522_gpio_configure();

    /* 拉低 NFC_GND 引脚，为模块提供参考地 */
#if defined(NFC_GND_Pin) && defined(NFC_GND_GPIO_Port)
    HAL_GPIO_WritePin(NFC_GND_GPIO_Port, NFC_GND_Pin, GPIO_PIN_RESET);
#endif

    /* 初始化 RC522 芯片 (传递平台 IO 接口) */
    RC522_Init((RC522_IO_t *)&s_rc522_io);
}

uint8_t RC522_Platform_ReadPins(void)
{
    uint8_t pins = 0u;

    if (HAL_GPIO_ReadPin(NFC_NSS_GPIO_Port, NFC_NSS_Pin) == GPIO_PIN_SET) {
        pins |= RC522_PIN_NSS;
    }
    if (HAL_GPIO_ReadPin(NFC_SCK_GPIO_Port, NFC_SCK_Pin) == GPIO_PIN_SET) {
        pins |= RC522_PIN_SCK;
    }
    if (HAL_GPIO_ReadPin(NFC_MOSI_GPIO_Port, NFC_MOSI_Pin) == GPIO_PIN_SET) {
        pins |= RC522_PIN_MOSI;
    }
    if (HAL_GPIO_ReadPin(NFC_MISO_GPIO_Port, NFC_MISO_Pin) == GPIO_PIN_SET) {
        pins |= RC522_PIN_MISO;
    }
    if (HAL_GPIO_ReadPin(NFC_RST_GPIO_Port, NFC_RST_Pin) == GPIO_PIN_SET) {
        pins |= RC522_PIN_RST;
    }
#if defined(SPI1_CS_Pin) && defined(SPI1_CS_GPIO_Port)
    if (HAL_GPIO_ReadPin(SPI1_CS_GPIO_Port, SPI1_CS_Pin) == GPIO_PIN_SET) {
        pins |= RC522_PIN_FLASH;
    }
#endif

    return pins;
}

uint8_t RC522_Platform_MosiSharesFlashCs(void)
{
#if defined(SPI1_CS_Pin) && defined(SPI1_CS_GPIO_Port)
    return (uint8_t)((NFC_MOSI_GPIO_Port == SPI1_CS_GPIO_Port) && (NFC_MOSI_Pin == SPI1_CS_Pin));
#else
    return 0u;
#endif
}
