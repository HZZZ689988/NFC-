#include "main.h"

#include "dma.h"
#include "gpio.h"
#include "rc522.h"
#include "usart.h"
#include "uart_drv.h"

#include <stdio.h>

static UartDrv_t s_diag_uart;

static void rc522_only_print_diag(void)
{
    uint8_t tag_type[2] = {0};
    uint8_t uid[4] = {0};

    uint8_t raw_version = RC522_ReadRegister(RC522_REG_VERSION);
    uint8_t raw_command = RC522_ReadRegister(RC522_REG_COMMAND);
    uint8_t raw_irq = RC522_ReadRegister(RC522_REG_COMIRQ);
    uint8_t raw_fifo = RC522_ReadRegister(RC522_REG_FIFOLEVEL);
    uint8_t raw_tx = RC522_ReadRegister(RC522_REG_TXCONTROL);
    uint8_t raw_error = RC522_ReadRegister(RC522_REG_ERROR);
    uint8_t raw_pins = RC522_Platform_ReadPins();

    RC522_ConfigISOType('A');

    uint8_t version = RC522_ReadRegister(RC522_REG_VERSION);
    uint8_t command = RC522_ReadRegister(RC522_REG_COMMAND);
    uint8_t irq = RC522_ReadRegister(RC522_REG_COMIRQ);
    uint8_t fifo = RC522_ReadRegister(RC522_REG_FIFOLEVEL);
    uint8_t tx = RC522_ReadRegister(RC522_REG_TXCONTROL);
    uint8_t error = RC522_ReadRegister(RC522_REG_ERROR);
    uint8_t pins = RC522_Platform_ReadPins();
    int request_status = (int)RC522_Request(RC522_PICC_REQALL, tag_type);
    int scan_status = (int)RC522_ScanCard(uid);
    uint8_t share = RC522_Platform_MosiSharesFlashCs();

    printf("RC522_ONLY:RAW=0x%02X|VER=0x%02X|CMD=0x%02X->0x%02X|IRQ=0x%02X->0x%02X|FIFO=0x%02X->0x%02X|TX=0x%02X->0x%02X|ERR=0x%02X->0x%02X|PINS=0x%02X->0x%02X|SHARE=%u|REQ=%d|TAG=%02X%02X|SCAN=%d|UID=%02X%02X%02X%02X\r\n",
           raw_version,
           version,
           raw_command,
           command,
           raw_irq,
           irq,
           raw_fifo,
           fifo,
           raw_tx,
           tx,
           raw_error,
           error,
           raw_pins,
           pins,
           (unsigned int)share,
           request_status,
           tag_type[0],
           tag_type[1],
           scan_status,
           uid[0],
           uid[1],
           uid[2],
           uid[3]);
}

void RC522_Only_Diag_Run(void)
{
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();

    UartDrv_Init(&s_diag_uart, &huart1);
    UartDrv_SetDebugPort(&s_diag_uart);

    printf("\r\nRC522-only diagnostic firmware started\r\n");
    printf("Pins: NSS=PB13 SCK=PB11 MOSI=PC4 MISO=PA1 RST=PA2 3V3/GND physical\r\n");
    printf("Goal: VER should be 0x91 or 0x92 before card tests matter\r\n");

    RC522_Platform_Init();

    while (1) {
        rc522_only_print_diag();
        HAL_Delay(1000);
    }
}
