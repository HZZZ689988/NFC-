# Firmware Framework

Target board:

- MCU: STM32F407VET6
- RTOS: FreeRTOS CMSIS-RTOS2
- NFC: RC522
- External flash: W25Q128 on SPI1
- Filesystem: LittleFS
- PC serial: USART1
- Network: ESP01S on USART6
- Display: OLED on I2C1

## Directories

```text
app/Inc, app/Src
  Attendance application layer. These files define stable module boundaries.

stm32/NFCAttend_Base
  Buildable STM32CubeMX/HAL/FreeRTOS base copied from Demo_W25Q128.

stm32/Bsp
  Provided board drivers: RC522, W25Q128, OLED, ESP01S, UART, key, LED, RTC.

third_party/littlefs
  LittleFS source from littlefs-project/littlefs.
```

## Integration Steps

1. Add `firmware/app/Src/*.c` and `firmware/third_party/littlefs/*.c` to the STM32 Makefile.
2. Add include paths:
   - `../../app/Inc`
   - `../../third_party/littlefs`
   - `../Bsp/w25qxx`
   - `../Bsp/NFC`
   - `../Bsp/ESP01`
3. Initialize BSP drivers in the boot task:
   - `W25QXX_Init`
   - `RC522_Platform_Init`
   - `ESP01S_Init`
4. Call `attendance_app_init`, then run periodic task functions:
   - `attendance_app_poll_nfc`
   - `attendance_app_poll_serial`
   - `attendance_app_poll_network`
5. Verify on hardware before marking firmware tasks done.
