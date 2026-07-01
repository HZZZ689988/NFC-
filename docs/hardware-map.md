# Hardware Map

Source: extracted CubeMX/BSP materials under `extracted/C_work`.

## MCU

- STM32F407VET6

## Peripherals

| Module | Interface | Pins |
| --- | --- | --- |
| OLED | I2C1 | PB6/PB7 |
| W25Q128 | SPI1 | PA5/PA6/PA7, CS PC4 |
| RC522 | Software SPI | NSS PB12, RST PC1, MOSI PC5, MISO PB13, SCK PB11 |
| PC serial | USART1 | PA9/PA10 |
| ESP01S | USART6 | PC6/PC7 |
| Keys | GPIO | PE1-PE6 |
| LEDs | GPIO | PE8-PE14 |
| Buzzer | TIM3_CH1 | PB4 |
| RTC | LSE | External low-speed crystal |

## RC522 Wiring

Use the HE32F4 RC522-accessible header pins provided on the board. Keep `PC4`
and `PA7` reserved for the onboard W25Q128 SPI1 flash because LittleFS storage
depends on that flash.

| RC522 module pin | STM32 pin |
| --- | --- |
| `NSS` / `SDA` / `CS` | `PB12` |
| `SCK` | `PB11` |
| `MOSI` | `PC5` |
| `MISO` | `PB13` |
| `RST` | `PC1` |
| `GND` | `GND` |
| `3.3V` | `3.3V` |

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
