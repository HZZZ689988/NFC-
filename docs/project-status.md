---
type: project-status
project: nfc-attendance
updated: 2026-07-02
status: in-progress
---

# Project Status

## Current Focus

The project is in board-verification mode with RC522 intentionally paused. The STM32 base links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling, upload ACK handling, OLED status display, RTC-backed timestamps, weather cache/display plumbing, local LED/buzzer feedback, and persistent device/network config writes. Non-RC522 paths now have board evidence for DAP flashing, USART1 command handling, W25Q128/LittleFS config and record CRC readback, sparse 24px OLED/weather display, ESP01S WiFi/NTP/RTC/TCP startup, heartbeat-to-server, simulated attendance upload ACK, DAP-reset RTC-derived timestamps, and physical OLED/LED/buzzer feedback.

## Implemented

- `pc_tool/`: Python/Tkinter upper-computer for serial communication, card issuing, image blocks, SQLite records, attendance import and weather cache diagnostics.
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
- `LIST:<count>` and `LIST:ALL` now stream stored attendance records as `REC:` lines after `LIST:COUNT`, including upload state as `UP=PENDING/DONE/FAILED`.
- `SIMATT:<uid>,<sid>,<type>` can inject a simulated attendance record through the app layer while RC522 is paused.
- `UITEST:<case>` can trigger sparse 24px OLED pages and local LED/buzzer feedback paths while RC522 is paused.
- `WEATHER?`, `WEATHERTEST:<text>`, `WEATHER!` and `TIME?` provide board-verification access to cached weather readback, test-cache writes, forced real ESP01S weather queries and app time-source state.
- The upper-computer serial client now treats `UID:`, `WEATHER:`, `TIME:` and `OK:*` replies as transaction terminators, so `READ`, weather/time commands, `ISSUE`, image writes and clear commands do not wait for avoidable timeouts.
- Firmware now links the OLED BSP and an `att_display` module; the display task shows device ID, record count, upload enable state, network state, weather placeholder, standby prompt and attendance OK/duplicate/invalid/error results.
- ESP01S startup can write NTP time into STM32 RTC; attendance timestamps use RTC-derived Unix seconds when RTC is valid and fall back to RTOS uptime otherwise.
- Firmware now stores latest weather text in LittleFS `/weather.txt` and reloads it into the OLED display model at app startup.
- Firmware feedback events now route attendance/network results to a FreeRTOS queue; L1/L2/L3/L4/L5 and TIM3_CH1 buzzer patterns indicate OK, invalid card, duplicate, fault and network-online states.
- The upper-computer now has a device config tab that sends segmented `CFG:` commands for device id, work mode, upload enable, anti-repeat interval, WiFi, server, weather and timezone; firmware saves those fields into `/config.bin` and reapplies display/network runtime state.
- The upper-computer device config tab can also read cached weather, write a short test weather cache string, force one real ESP01S weather query and read the current device time source.
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
- STM32 firmware size after this slice: `text=105208`, `data=496`, `bss=44360`.

## Board Validated

- CMSIS-DAP flashing and verify/reset.
- USART1 over `COM3` responds to `PING`, `CFG?`, `LIST:1`, `LIST:ALL` and `OLEDTEST` after repeated commands.
- W25Q128/LittleFS mounts and loads persistent `/config.bin`.
- Empty board storage returns `LIST:COUNT=0` and `LIST:END`; simulated records are appended and read back through the CRC-checked record path.
- OLED initializes and displays the sparse 24px `OLEDTEST` and weather pages.
- `WEATHERTEST:Sunny 20C` creates/updates `/weather.txt`; `WEATHER?` reads it back as `WEATHER:Sunny 20C` after DAP reset.
- ESP01S connects WiFi, syncs NTP into RTC, connects TCP to `server/server.py` and sends heartbeat.
- After a DAP reset, a pre-NTP simulated attendance record used RTC-derived Unix seconds instead of RTOS uptime fallback.
- `TIME?` reports the app time source over `COM3`; after a reported board power cycle, current board result was `TIME:1783011952|VALID=1`.
- Weather location is configured for Hangzhou as `WLOC=30.267:120.153`; forced real weather query still returns `ERR:NOT_READY` until a private Seniverse `WKEY` is configured.
- Simulated attendance upload reaches `server/server.py`, receives `ACK:UPLOAD:<seq>` and persists `UP=DONE` in LittleFS.
- `UITEST:READY/OK/DUP/INVALID/ERROR/NETOK/NETERR` commands are accepted over `COM3` and route through the same display/feedback callbacks as runtime events.
- Physical observation confirmed the sparse 24px OLED pages, L1-L5 LED mapping and TIM3_CH1 PB4 buzzer feedback for the simulated `UITEST` cases.

## Not Yet Hardware Validated

- RC522 real-card UID forced-consistency issuing.
- RC522 card account block read/write with CRC16 and UID consistency on a real card.
- RC522 image-card write flow: 24 portrait blocks, 10 name blocks, 10 department blocks and `UPDATEIMG`.
- USART1 `LIST:<count>` / `LIST:ALL` record streaming after real persistent attendance records exist.
- ESP01S real RC522-driven attendance `UPLOAD:` plus `ACK:UPLOAD:<seq>` round trip.
- Real ESP01S weather API query with a configured private Seniverse `WKEY`.
- Strict RTC retention timing proof before fresh NTP sync after full power-loss/VBAT conditions.
- Physical observation of OLED weather page after real ESP01S weather query.
- Physical observation of LED and TIM3_CH1 PB4 feedback after real RC522 card events.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- Network ACK parsing and `att_storage_mark_uploaded()` are board-validated with an injected attendance record; real RC522-driven upload still needs card input.
- RC522 register communication is paused and remains the main blocker for the full attendance loop.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
