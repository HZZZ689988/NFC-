---
type: project-status
project: nfc-attendance
updated: 2026-06-29
status: in-progress
---

# Project Status

## Current Focus

The project is in firmware integration and host-verification mode. The STM32 base now links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling and upload ACK handling. Hardware validation is still pending.

## Implemented

- `pc_tool/`: Python/Tkinter upper-computer for serial communication, card issuing, image blocks, SQLite records and attendance import.
- `firmware/stm32/NFCAttend_Base/`: STM32 HAL/FreeRTOS base copied from `Demo_W25Q128`.
- `firmware/stm32/Bsp/`: BSP drivers for W25Q128, RC522, OLED, ESP01S, UART, Key, LED and related modules.
- `firmware/third_party/littlefs/`: LittleFS source.
- `firmware/app/`: CRC16, LittleFS storage, serial protocol, UID consistency card operations, network upload and application entry modules.
- `server/`: stage-3 TCP upload and heartbeat test server.
- `docs/requirements.md`: formal project requirements.
- `docs/reference-materials.md`: reference-material traceability and repository inclusion notes.
- FreeRTOS tasks now initialize the attendance app, dispatch USART1 protocol lines, poll RC522 for local attendance records, initialize ESP01S on USART6, periodically schedule heartbeat/upload attempts, and dispatch ESP01S TCP ACK lines to the network layer.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- `firmware/app/tests/test_att_protocol_host.c` passed.
- `firmware/app/tests/test_attendance_serial_host.c` passed.
- `firmware/app/tests/test_attendance_nfc_host.c` passed.
- `firmware/app/tests/test_att_network_host.c` passed.
- `firmware/app/tests/test_attendance_network_host.c` passed.
- `make clean; make` passed in `firmware/stm32/NFCAttend_Base` with LittleFS, RC522 and ESP01S app modules linked.
- The STM32 firmware build is clean under the current `-Wall` settings.

## Not Yet Hardware Validated

- LittleFS mount/format/read/write on W25Q128.
- RC522 real-card UID forced-consistency issuing.
- ESP01S WiFi, NTP, weather, heartbeat and TCP upload.
- OLED page display.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- Network ACK parsing is host-tested. Firmware marks records uploaded only after receiving `ACK:UPLOAD:<seq>`, but this path still needs board-side ESP01S/TCP validation.
- ESP01S startup is build-linked and scheduled, but WiFi/TCP/NTP behavior still needs board-side evidence.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
