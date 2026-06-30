# NFC Attendance System

NFC attendance system for STM32F407VET6, RC522, W25Q128, OLED, ESP01S and a Python upper-computer tool.

## Project Layout

```text
firmware/
  app/                         Application framework for attendance logic
  stm32/NFCAttend_Base/        STM32CubeMX/HAL/FreeRTOS base copied from Demo_W25Q128
  stm32/Bsp/                   Board support drivers from the provided materials
  third_party/littlefs/        LittleFS source files
pc_tool/                       Python/Tkinter upper-computer tool
server/                        Minimal TCP server for stage-3 upload tests
docs/                          Plans, protocol, hardware map, bring-up notes
tools/                         Helper scripts
```

## Current Scope

- Stage 1: card issuing, UID read/clear, image block transfer, local PC database.
- Stage 2: firmware attendance core, RC522 card read/write, LittleFS-backed config and records.
- Stage 3: required networking with ESP01S, NTP RTC sync, weather display, TCP upload, offline retry and heartbeat.

## Key Decisions

- CRC variant: `CRC16-CCITT-FALSE` (`poly=0x1021`, `init=0xFFFF`, `xorout=0x0000`, no reflection).
- Storage: W25Q128 + LittleFS.
- Card issuing: UID must be read again by MCU and match the UID in the `ISSUE` command before writing.
- Default config: `device_id=1`, in/out mode, upload enabled, network inactive until WiFi/server config is set.

## Quick Start

Run the upper-computer tool:

```powershell
cd pc_tool
python -m pip install -r requirements.txt
python run.py
```

Build the STM32 base after installing `arm-none-eabi-gcc`:

```powershell
cd firmware/stm32/NFCAttend_Base
make
```

Run the firmware application host tests after installing a native C compiler
such as `gcc`, `clang`, `zig` or MSVC `cl`:

```powershell
python firmware/app/tests/run_host_tests.py
```

Run the main host verification suite:

```powershell
python tools/run_verification.py
```

Add `--firmware-build` to include `make clean`, `make` and the ELF segment
permission check.

The application framework under `firmware/app` is linked into the STM32 base
Makefile and integrated from `Core/Src/freertos.c`.
