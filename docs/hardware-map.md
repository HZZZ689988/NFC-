# Hardware Map

Source: extracted CubeMX/BSP materials under `extracted/C_work`.

## MCU

- STM32F407VET6

## Peripherals

| Module | Interface | Pins |
| --- | --- | --- |
| OLED | I2C1 | PB6/PB7 |
| W25Q128 | SPI1 | PA5/PA6/PA7, CS PC4 |
| RC522 | Software SPI | NSS PE15, RST PB15, MOSI PA0, MISO PB13, SCK PD9 |
| PC serial | USART1 | PA9/PA10 |
| ESP01S | USART6 | PC6/PC7 |
| Keys | GPIO | PE1-PE6 |
| LEDs | GPIO | PE8-PE14 |
| Buzzer | TIM3_CH1 | PB4 |
| RTC | LSE | External low-speed crystal |

## Firmware Base

Recommended base:

```text
firmware/stm32/NFCAttend_Base
```

Original source:

```text
extracted/C_work/BSP/Demo_W25Q128
```
