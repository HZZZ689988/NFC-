# Hardware Map

Source: extracted CubeMX/BSP materials under `extracted/C_work`.

## MCU

- STM32F407VET6

## Peripherals

| Module | Interface | Pins |
| --- | --- | --- |
| OLED | I2C1 | PB6/PB7 |
| W25Q128 | SPI1 | PA5/PA6/PA7, CS PC4 |
| RC522 | Software SPI | Active diagnostic/app map: GND PB10, NSS PE15, SCK PD9, MOSI PA0, MISO PB13, RST PB15 |
| PC serial | USART1 | PA9/PA10 |
| ESP01S | USART6 | PC6/PC7 |
| Keys | GPIO | PE1-PE6 |
| LEDs | GPIO | PE8-PE14 |
| Buzzer | TIM3_CH1 | PB4 |
| RTC | LSE | External low-speed crystal |

## RC522 Wiring

The current active firmware and RC522-only diagnostic image use the remapped
header with PB10 acting as a controlled low level for the RC522 ground pin:

| RC522 module pin | STM32 pin/header label |
| --- | --- |
| `3.3V` | `3.3V` |
| `GND` | `PB10`, firmware drives push-pull low |
| `RST` | `PB15` |
| `MISO` | `PB13` |
| `MOSI` | `PA0` |
| `SCK` | `PD9` |
| `NSS` / `SDA` / `CS` | `PE15` |
| `IRQ` | not connected |

Current active firmware assumptions:

```text
RC522 VCC        -> 3.3V
RC522 GND        -> PB10, firmware-controlled low
RC522 NSS/SDA/CS -> PE15
RC522 SCK        -> PD9
RC522 MOSI       -> PA0
RC522 MISO       -> PB13
RC522 RST        -> PB15
RC522 IRQ        -> not connected
```

Older candidate header maps are still scanned by the RC522-only diagnostic as
`H1` and `H2`, but they are not the active application mapping:

- `H1`: NSS PA2, SCK PA1, MOSI PC4, MISO PB11, RST PB13.
- `H2`: NSS PC1, SCK PA7, MOSI PC5, MISO PB12, no RST candidate.

The current board-side RC522 diagnostic has returned `RC522_VER=0x00` on the
old hardware and `RC522_VER=0xFF` on the replacement hardware. Neither is a
valid MFRC522 version value. A healthy MFRC522 clone normally returns `0x91` or
`0x92`. The BSP prebuilt RC522 demo starts on the board but does not report a
UID, and the enhanced diagnostic reports `DEMOLOW=00/00/00` plus
`GPIOCHK=NSS=3/SCK=3/MOSI=3/RST=3/GNDL=1`. These results show that the MCU-side
GPIOs can be driven, while the RC522 still does not return a valid SPI register
response. With a module that works on another board, prioritize this board's
RC522 signal path and assumptions: PB10-as-ground margin under load,
PE15/PD9/PA0/PB13/PB15 continuity and whether MISO reaches PB13.

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
