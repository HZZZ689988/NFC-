---
type: project-status
project: nfc-attendance
updated: 2026-07-02
status: in-progress
---

# Project Status

## Current Focus

The project is in board-verification mode with RC522 intentionally paused. The STM32 base links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling, upload ACK handling, OLED status display, RTC-backed timestamps, weather cache/display plumbing, local LED/buzzer feedback, and persistent device/network config writes. Non-RC522 paths now have board evidence for DAP flashing, USART1 command handling, upper-computer serial-client interop, W25Q128/LittleFS config and record CRC readback, sparse 24px OLED/weather display, persistent config write/reload, ESP01S WiFi/NTP/RTC/TCP startup, real Seniverse weather query/cache, heartbeat-to-server, simulated attendance upload ACK, anti-repeat substitute validation, upload-enable gating, outage-time pending record retention, recovery upload, DAP-reset RTC-derived timestamps, physical OLED/LED/buzzer feedback, and upper-computer import/export against live board records. Upper-computer SQLite and CSV persistence are host-validated for people, issue logs, lost-card marks, attendance import, upload-state refresh and UTF-8-SIG export.

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
- `SIMATT:<uid>,<sid>,<type>` can inject a simulated attendance record through the app layer while RC522 is paused, now using the same anti-repeat state as local NFC polling.
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
- The upper-computer stores board `UP=PENDING/DONE/FAILED` state in SQLite, displays it in the attendance-record table and exports it to CSV; repeated imports of the same `DEV+SEQ` refresh status without duplicating rows.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- Upper-computer tests cover people upsert, issue-log insert, lost-card flag updates, `REC:` upload-state parsing, duplicate `DEV+SEQ` refresh from `PENDING` to `DONE`, SQLite schema migration and UTF-8-SIG CSV export.
- `pc_tool.nfc_attendance_tool.SerialClient` was validated against the live board on `COM3` for `CFG?`, `TIME?`, `WEATHER?`, `WEATHERTEST`, `WEATHER!` and `LIST:2`.
- `python firmware/app/tests/run_host_tests.py` builds and runs the native C host test executables for protocol routing, USART line buffering, local NFC polling, ESP01S network parsing and network polling schedule.
- `python tools/run_verification.py` is the default host verification entrypoint; `--firmware-build` adds STM32 clean build and ELF segment permission checks.
- Native display host tests cover the OLED status model text for standby, network state, weather, attendance OK and event timeout behavior.
- ARM GCC compile-only checks also passed for the same protocol, serial, NFC and network test sources.
- `make clean; make` passed in `firmware/stm32/NFCAttend_Base` with LittleFS, RC522, ESP01S, OLED, RTC, LED and MIDI buzzer app modules linked and no warning lines in the build log.
- `arm-none-eabi-readelf -l build/Demo_W25Q128.elf` shows the Flash `PT_LOAD` segment as `R E` and RAM `PT_LOAD` segments as `RW`, with no `RWE`/`RWX` load segment.
- STM32 firmware size after this slice: `text=105592`, `data=496`, `bss=45392`.

## Board Validated

- CMSIS-DAP flashing and verify/reset.
- USART1 over `COM3` responds to `PING`, `CFG?`, `LIST:1`, `LIST:ALL` and `OLEDTEST` after repeated commands.
- W25Q128/LittleFS mounts and loads persistent `/config.bin`.
- Segmented `CFG:` writes for device/mode/upload/repeat/timezone/WiFi/server/weather fields persist across CMSIS-DAP reset and can be restored to production values.
- Empty board storage returns `LIST:COUNT=0` and `LIST:END`; simulated records are appended and read back through the CRC-checked record path.
- OLED initializes and displays the sparse 24px `OLEDTEST` and weather pages.
- `WEATHERTEST:Sunny 20C` creates/updates `/weather.txt`; `WEATHER?` reads it back as `WEATHER:Sunny 20C` after DAP reset.
- ESP01S connects WiFi, syncs NTP into RTC, connects TCP to `server/server.py` and sends heartbeat.
- After a DAP reset, a pre-NTP simulated attendance record used RTC-derived Unix seconds instead of RTOS uptime fallback.
- `TIME?` reports the app time source over `COM3`; after a reported board power cycle, current board result was `TIME:1783011952|VALID=1`.
- Weather location is configured for Hangzhou as `WLOC=30.267:120.153`; a private Seniverse `WKEY` was validated locally, and board `WEATHER!` returned `WEATHER:Light 23C`.
- Simulated attendance upload reaches `server/server.py`, receives `ACK:UPLOAD:<seq>` and persists `UP=DONE` in LittleFS; with the server endpoint unavailable, a simulated record remains `UP=PENDING` and later changes to `UP=DONE` after restoring the endpoint.
- `SIMATT` now rejects same-UID attendance inside `REPEAT=60` with `ERR:DUPLICATE`; `UPLOAD=0` keeps new simulated records local as `UP=PENDING`, and restoring `UPLOAD=1` retries them to `UP=DONE`.
- CRC-framed `$PING*6427` returns `OK:PONG`; a bad CRC frame returns `ERR:CRC`; bad `LIST` arguments return `ERR:ARG`; no-card `READ`/`ISSUE`/`CLEAR` return `ERR:NO_CARD`.
- `UITEST:READY/OK/DUP/INVALID/ERROR/NETOK/NETERR` commands are accepted over `COM3` and route through the same display/feedback callbacks as runtime events.
- Physical observation confirmed the sparse 24px OLED pages, L1-L5 LED mapping and TIM3_CH1 PB4 buzzer feedback for the simulated `UITEST` cases.
- The upper-computer `SerialClient` read live `LIST:5` records from `COM3`, imported five `REC:` rows with `UP=DONE` into a temporary SQLite database and exported a CSV with the `upload_state` column.

## Not Yet Hardware Validated

- RC522 real-card UID forced-consistency issuing.
- RC522 card account block read/write with CRC16 and UID consistency on a real card.
- RC522 image-card write flow: 24 portrait blocks, 10 name blocks, 10 department blocks and `UPDATEIMG`.
- ESP01S real RC522-driven attendance `UPLOAD:` plus `ACK:UPLOAD:<seq>` round trip.
- Strict RTC retention timing proof before fresh NTP sync after full power-loss/VBAT conditions.
- Physical observation of LED and TIM3_CH1 PB4 feedback after real RC522 card events.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- Network ACK parsing and `att_storage_mark_uploaded()` are board-validated with an injected attendance record; real RC522-driven upload still needs card input.
- RC522 register communication is paused and remains the main blocker for the full attendance loop.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
