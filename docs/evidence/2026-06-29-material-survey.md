---
type: evidence
date: 2026-06-29
source: extracted-project
---

# Material Survey

Extracted materials include:

- `NFCAttend/NFCAttend.ioc`: CubeMX config.
- `BSP/Demo_W25Q128`: selected firmware base.
- `BSP/Bsp/w25qxx`: W25Q128 driver.
- `BSP/Bsp/NFC`: RC522 driver.
- `BSP/Bsp/OLED`: SSD1306/OLED GUI driver.
- `BSP/Bsp/ESP01`: ESP01S WiFi/NTP/weather/TCP driver.
- `BSP/Bsp/UartDrv`: UART driver.
- `BSP/Bsp/Key`, `BSP/Bsp/LED`, `BSP/Bsp/RTC`: board support modules.

Relevant hardware facts:

- MCU: STM32F407VET6.
- W25Q128 uses SPI1 and CS PC4.
- PC serial uses USART1 PA9/PA10.
- ESP01S uses USART6 PC6/PC7.
- RC522 uses software SPI pins from the provided IOC/materials.
