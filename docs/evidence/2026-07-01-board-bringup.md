---
type: evidence
target: board-bringup
date: 2026-07-01
hardware-validated: partial
---

# Board Bring-Up Log

## Setup

- Board: STM32F407VET6 NFC attendance board.
- Programmer: CMSIS-DAP over SWD.
- Programmer USB device: `VID_C251&PID_F001`, serial `0001A0000000`.
- Debug UART: same USB composite device, `COM3`.
- UART settings: `115200 8N1`.
- Firmware image: `firmware/stm32/NFCAttend_Base/build/Demo_W25Q128.elf`.
- Flash tool: xPack OpenOCD `0.12.0-7`.

The USB composite device exposes both:

- CMSIS-DAP HID interface for SWD download/debug.
- USB serial interface `COM3` for firmware USART1 communication.

## Download

Command:

```powershell
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/Demo_W25Q128.elf verify reset exit"
```

Observed result:

```text
Info : CMSIS-DAP: FW Version = 2.0.0
Info : SWD DPIDR 0x2ba01477
Info : [stm32f4x.cpu] Cortex-M4 r0p1 processor detected
Info : device id = 0x101f6413
Info : flash size = 512 KiB
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

Result: firmware download, verify and reset passed.

## Boot Log

After reset via OpenOCD while listening on `COM3`:

```text
Attendance app ready
NFC Attendance app started
K3=status K6=storage bootstrap L1=OK L2=invalid L3=duplicate L4=fault L5=network
```

## Serial Protocol Checks

Commands were sent as CRC frames over `COM3`.

| Command payload | Sent frame | Response |
| --- | --- | --- |
| `PING` | `$PING*6427\n` | `OK:PONG\n` |
| `CFG?` | `$CFG?*8D6A\n` | `CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=|PORT=0|TZ=8|SSID=|WLOC=\n` |
| `LIST:1` | `$LIST:1*7632\n` | `LIST:COUNT=0\nLIST:END\n` |
| `READ` | `$READ*DC73\n` | `ERR:NO_CARD\n` |

Result:

- USART1 command channel is working through `COM3`.
- CRC-framed protocol dispatch is working.
- Default device config can be loaded from storage through `CFG?`.
- Record listing path is working with an empty record file/state.
- RC522 no-card path is working at protocol level.

## Not Yet Covered

- W25Q128 JEDEC ID was not directly captured in this session. The firmware prints it from the K6 storage-bootstrap path.
- RC522 UID read with a real card was not tested.
- Card issue/readback/clear was not tested.
- OLED, LEDs, buzzer and ESP01S network paths were not tested.
- The known board-level risk remains: active RC522 column A maps `MOSI` to `PC4`, while the inherited base project also labels `PC4` as W25Q128 CS. RC522 and W25Q128 must still be validated together under real card operations.
