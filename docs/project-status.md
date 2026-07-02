---
type: project-status
project: nfc-attendance
updated: 2026-07-02
status: in-progress
---

# Project Status

## Current Focus

The project is in board-verification mode with RC522 intentionally paused. The STM32 base links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling, upload ACK handling, OLED status display, RTC-backed timestamps, weather cache/display plumbing, local LED/buzzer feedback, and persistent device/network config writes. Non-RC522 paths now have board evidence for DAP flashing, USART1 command handling, W25Q128/LittleFS config, empty record listing, sparse 24px OLED test display, ESP01S WiFi/NTP/RTC/TCP startup and heartbeat-to-server.

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
- `UPLOAD=0` disables heartbeat and pending-record upload attempts while keeping NTP time sync and weather cache/display polling active when the ESP01S network path is ready.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- `python firmware/app/tests/run_host_tests.py` builds and runs the native C host test executables for protocol routing, USART line buffering, local NFC polling, ESP01S network parsing and network polling schedule.
- `python tools/run_verification.py` is the default host verification entrypoint; `--firmware-build` adds STM32 clean build and ELF segment permission checks.
- Native display host tests cover the OLED status model text for standby, network state, weather, attendance OK and event timeout behavior.
- ARM GCC compile-only checks also passed for the same protocol, serial, NFC and network test sources.
- `make clean; make` passed in `firmware/stm32/NFCAttend_Base` with LittleFS, RC522, ESP01S, OLED, RTC, LED and MIDI buzzer app modules linked and no warning lines in the build log.
- `arm-none-eabi-readelf -l build/Demo_W25Q128.elf` shows the Flash `PT_LOAD` segment as `R E` and RAM `PT_LOAD` segments as `RW`, with no `RWE`/`RWX` load segment.
- STM32 firmware size after this slice: `text=103096`, `data=496`, `bss=44360`.

## Board Validated

- CMSIS-DAP flashing and verify/reset.
- USART1 over `COM3` responds to `PING`, `CFG?`, `LIST:1`, `LIST:ALL` and `OLEDTEST` after repeated commands.
- W25Q128/LittleFS mounts and loads persistent `/config.bin`.
- Empty board storage returns `LIST:COUNT=0` and `LIST:END`.
- OLED initializes and displays the sparse 24px `OLEDTEST` page.
- ESP01S connects WiFi, syncs NTP into RTC, connects TCP to `server/server.py` and sends heartbeat.

## Not Yet Hardware Validated

- RC522 real-card UID forced-consistency issuing.
- RC522 card account block read/write with CRC16 and UID consistency on a real card.
- RC522 image-card write flow: 24 portrait blocks, 10 name blocks, 10 department blocks and `UPDATEIMG`.
- USART1 `LIST:<count>` / `LIST:ALL` record streaming after real persistent attendance records exist.
- ESP01S real attendance `UPLOAD:` plus `ACK:UPLOAD:<seq>` round trip.
- Weather query/cache.
- RTC retention and RTC-derived attendance timestamps on real LSE/VBAT conditions.
- OLED normal runtime pages, including standby, attendance result, network state and weather pages.
- LED and TIM3_CH1 buzzer feedback on PE8-PE12/PB4 for attendance and network state events.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- Network ACK parsing is host-tested. Firmware marks records uploaded only after receiving `ACK:UPLOAD:<seq>`, but this path still needs board-side validation with a real or injected attendance record.
- RC522 register communication is paused and remains the main blocker for the full attendance loop.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
