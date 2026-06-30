---
type: project-status
project: nfc-attendance
updated: 2026-06-30
status: in-progress
---

# Project Status

## Current Focus

The project is in full-feature firmware integration and host-verification mode. The STM32 base now links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling, upload ACK handling, OLED status display, RTC-backed timestamps, weather cache/display plumbing, local LED/buzzer feedback, and persistent device/network config writes. Card account reads, image-card block writes, serial attendance record streaming, OLED status pages, RTC/weather integration, LED/buzzer feedback events and `CFG:` config writes are implemented in firmware, but hardware validation is still pending.

## Implemented

- `pc_tool/`: Python/Tkinter upper-computer for serial communication, card issuing, image blocks, SQLite records and attendance import.
- `firmware/stm32/NFCAttend_Base/`: STM32 HAL/FreeRTOS base copied from `Demo_W25Q128`.
- `firmware/stm32/Bsp/`: BSP drivers for W25Q128, RC522, OLED, ESP01S, UART, Key, LED and related modules.
- `firmware/third_party/littlefs/`: LittleFS source.
- `firmware/app/`: CRC16, LittleFS storage, serial protocol, UID consistency card operations, network upload, weather cache, OLED display state and application entry modules.
- `server/`: stage-3 TCP upload and heartbeat test server.
- `docs/requirements.md`: formal project requirements.
- `docs/reference-materials.md`: reference-material traceability and repository inclusion notes.
- FreeRTOS tasks now initialize the attendance app, dispatch USART1 protocol lines, poll RC522 for local attendance records, refresh OLED display pages, initialize ESP01S on USART6, periodically schedule NTP status checks, weather queries, heartbeat/upload attempts, and dispatch ESP01S TCP ACK lines to the network layer.
- Firmware now writes and reads the card account block at Mifare sector 0 block 1 with UID, SID, points, card type and CRC16.
- Firmware now handles image-card block commands `IMGAxx`, `IMGNxx`, `IMGDxx` and `UPDATEIMG`, writing only Mifare data blocks and requiring a complete same-UID image update session.
- Local NFC attendance uses the validated card account SID instead of UID-only records.
- `LIST:N` and `LIST:ALL` now stream stored attendance records as `REC:` lines after `LIST:COUNT`.
- The upper-computer serial client now treats `UID:` and `OK:*` replies as transaction terminators, so `READ`, `ISSUE`, image writes and clear commands do not wait for avoidable timeouts.
- Firmware now links the OLED BSP and an `att_display` module; the display task shows device ID, record count, upload enable state, network state, weather placeholder, standby prompt and attendance OK/duplicate/invalid/error results.
- ESP01S startup can write NTP time into STM32 RTC; attendance timestamps use RTC-derived Unix seconds when RTC is valid and fall back to RTOS uptime otherwise.
- Firmware now stores latest weather text in LittleFS `/weather.txt` and reloads it into the OLED display model at app startup.
- Firmware feedback events now route attendance/network results to a FreeRTOS queue; L1/L2/L3/L4/L5 and TIM3_CH1 buzzer patterns indicate OK, invalid card, duplicate, fault and network-online states.
- The upper-computer now has a device config tab that sends segmented `CFG:` commands for device id, work mode, upload enable, anti-repeat interval, WiFi, server, weather and timezone; firmware saves those fields into `/config.bin` and reapplies display/network runtime state.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- ARM GCC compile-only checks passed for the protocol, serial, NFC and network test sources because no native C compiler is installed in the current environment.
- `make clean; make` passed in `firmware/stm32/NFCAttend_Base` with LittleFS, RC522, ESP01S, OLED, RTC, LED and MIDI buzzer app modules linked.
- STM32 firmware size after this slice: `text=98928`, `data=488`, `bss=46776`.

## Not Yet Hardware Validated

- LittleFS mount/format/read/write on W25Q128.
- RC522 real-card UID forced-consistency issuing.
- RC522 card account block read/write with CRC16 and UID consistency on a real card.
- RC522 image-card write flow: 24 portrait blocks, 10 name blocks, 10 department blocks and `UPDATEIMG`.
- USART1 `LIST:N` / `LIST:ALL` record streaming against real persistent records.
- USART1 `CFG:` config writes to `/config.bin` and resulting ESP01S/display runtime config on the real board.
- ESP01S WiFi, NTP-to-RTC, weather query/cache, heartbeat and TCP upload.
- RTC retention and RTC-derived attendance timestamps on real LSE/VBAT conditions.
- OLED page display on real I2C1 hardware, including standby, attendance result, network state and weather pages.
- LED and TIM3_CH1 buzzer feedback on PE8-PE12/PB4 for attendance and network state events.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable. OLED `GUISlim.c` also emits existing `-Wmisleading-indentation` warnings during the linked build.
- Network ACK parsing is host-tested. Firmware marks records uploaded only after receiving `ACK:UPLOAD:<seq>`, but this path still needs board-side ESP01S/TCP validation.
- ESP01S startup is build-linked and scheduled, but WiFi/TCP/NTP/weather behavior still needs board-side evidence.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
- The STM32 linker still warns that `build/Demo_W25Q128.elf` has a LOAD segment with RWX permissions; this is build-linked but should be cleaned up in the linker script.
