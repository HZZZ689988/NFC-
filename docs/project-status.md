---
type: project-status
project: nfc-attendance
updated: 2026-06-29
status: in-progress
---

# Project Status

## Current Focus

The overall project framework is established. The repository now includes firmware base code, application-layer firmware modules, LittleFS, the upper-computer tool, a stage-3 test server and structured project documentation.

## Implemented

- `pc_tool/`: Python/Tkinter upper-computer for serial communication, card issuing, image blocks, SQLite records and attendance import.
- `firmware/stm32/NFCAttend_Base/`: STM32 HAL/FreeRTOS base copied from `Demo_W25Q128`.
- `firmware/stm32/Bsp/`: BSP drivers for W25Q128, RC522, OLED, ESP01S, UART, Key, LED and related modules.
- `firmware/third_party/littlefs/`: LittleFS source.
- `firmware/app/`: CRC16, LittleFS storage, serial protocol, UID consistency card operations, network upload and application entry modules.
- `server/`: stage-3 TCP upload and heartbeat test server.
- `docs/requirements.md`: formal project requirements.
- `docs/reference-materials.md`: reference-material traceability and repository inclusion notes.
- FreeRTOS demo task now calls the W25Q128/LittleFS storage bootstrap path and reports config status over USART1.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- `make` passed in `firmware/stm32/NFCAttend_Base`.
- `firmware/app/Src/*.c` passed ARM GCC compile checks as standalone objects.
- `firmware/stm32/NFCAttend_Base` now links LittleFS plus storage/protocol core modules.
- `make clean; make` passes after wiring the storage bootstrap into `Core/Src/freertos.c`.

## Not Yet Hardware Validated

- LittleFS mount/format/read/write on W25Q128.
- RC522 real-card UID forced-consistency issuing.
- ESP01S WiFi, NTP, weather and TCP upload.
- OLED page display.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- `firmware/app` is only partially linked. Storage/protocol core is linked; RC522/ESP01S app modules still need GPIO/UART6 task integration.
- Only the storage bootstrap path is called by an actual FreeRTOS task; card, protocol and network workflows still need task integration.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
