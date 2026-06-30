---
type: project-status
project: nfc-attendance
updated: 2026-06-30
status: in-progress
---

# Project Status

## Current Focus

The project is in full-feature firmware integration and host-verification mode. The STM32 base now links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling and upload ACK handling. Card account reads and serial attendance record streaming are implemented in firmware, but hardware validation is still pending.

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
- Firmware now writes and reads the card account block at Mifare sector 0 block 1 with UID, SID, points, card type and CRC16.
- Local NFC attendance uses the validated card account SID instead of UID-only records.
- `LIST:N` and `LIST:ALL` now stream stored attendance records as `REC:` lines after `LIST:COUNT`.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- ARM GCC compile-only checks passed for the protocol, serial, NFC and network test sources because no native C compiler is installed in the current environment.
- `make clean; make` passed in `firmware/stm32/NFCAttend_Base` with LittleFS, RC522 and ESP01S app modules linked.
- STM32 firmware size after this slice: `text=80096`, `data=488`, `bss=43480`.

## Not Yet Hardware Validated

- LittleFS mount/format/read/write on W25Q128.
- RC522 real-card UID forced-consistency issuing.
- RC522 card account block read/write with CRC16 and UID consistency on a real card.
- USART1 `LIST:N` / `LIST:ALL` record streaming against real persistent records.
- ESP01S WiFi, NTP, weather, heartbeat and TCP upload.
- OLED page display.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- Network ACK parsing is host-tested. Firmware marks records uploaded only after receiving `ACK:UPLOAD:<seq>`, but this path still needs board-side ESP01S/TCP validation.
- ESP01S startup is build-linked and scheduled, but WiFi/TCP/NTP behavior still needs board-side evidence.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
- The STM32 linker still warns that `build/Demo_W25Q128.elf` has a LOAD segment with RWX permissions; this is build-linked but should be cleaned up in the linker script.
