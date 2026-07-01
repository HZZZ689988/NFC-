# Hardware Map

Source: extracted CubeMX/BSP materials under `extracted/C_work`.

## MCU

- STM32F407VET6

## Peripherals

| Module | Interface | Pins |
| --- | --- | --- |
| OLED | I2C1 | PB6/PB7 |
| W25Q128 | SPI1 | PA5/PA6/PA7, CS PC4 |
| RC522 | Software SPI | Header column A: NSS PB13, SCK PB11, MOSI PC4, MISO PA1, RST PA2 |
| PC serial | USART1 | PA9/PA10 |
| ESP01S | USART6 | PC6/PC7 |
| Keys | GPIO | PE1-PE6 |
| LEDs | GPIO | PE8-PE14 |
| Buzzer | TIM3_CH1 | PB4 |
| RTC | LSE | External low-speed crystal |

## RC522 Wiring

Use one complete RC522 header column. Do not mix pins between the two columns.
The firmware is configured for column A, using the header order
`3.3V/GND/RST/MISO/MOSI/SCK/NSS/extra`.

| RC522 module pin | Column A STM32 pin/header label |
| --- | --- |
| `3.3V` | `3.3V` |
| `GND` | `GND` |
| `RST` | `PA2` |
| `MISO` | `PA1` |
| `MOSI` | `PC4` |
| `SCK` | `PB11` |
| `NSS` / `SDA` / `CS` | `PB13` |
| `IRQ` / unused extra pin | `ETH` |

Column B is physically present as `3.3V`, `GND`, `PC1`, `PA7`, `PC5`, `PB12`,
`NC`. It is not the active firmware mapping because the last RC522 signal would
land on `NC` with the same header ordering. Its extra label is `RM11`.

Column A shares `PC4` with the existing W25Q128 chip-select label in the base
project. Treat the real board validation of RC522 plus W25Q128 as required
before marking hardware complete.

Do not power the RC522 from 5 V.

## Download

Use the DAP/CMSIS-DAP programmer over SWD. The firmware OpenOCD configuration is
`firmware/stm32/NFCAttend_Base/openocd.cfg` and already selects
`interface/cmsis-dap.cfg`.

## Firmware Base

Recommended base:

```text
firmware/stm32/NFCAttend_Base
```

Original source:

```text
extracted/C_work/BSP/Demo_W25Q128
```
