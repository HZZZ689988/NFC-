---
type: project-status
project: nfc-attendance
updated: 2026-07-03
status: in-progress
---

# Project Status

## Current Focus

The project is in integrated board-verification mode. RC522 SPI communication is healthy on the complete firmware (`RC522_VER=0x92`, register write/read `RW=1`), and prior same-branch board runs verified real-card UID read, issuing, attendance append and record import. The STM32 base links the attendance app, RC522, LittleFS, USART1 protocol dispatch, local NFC polling, ESP01S network upload scheduling, upload ACK handling, OLED status display, RTC-backed timestamps, weather cache/display plumbing, local LED/buzzer feedback, persistent device/network config writes, and a `CARDLOCK` upper-computer issuing mode that pauses automatic attendance polling during serial jobs. Non-RC522 paths now have board evidence for DAP flashing, USART1 command handling, upper-computer serial-client interop, W25Q128/LittleFS config and record CRC readback, sparse 24px OLED/weather display, persistent config write/reload, ESP01S WiFi/NTP/RTC/TCP startup, real Seniverse weather query/cache, heartbeat-to-server, simulated attendance upload ACK, anti-repeat substitute validation, upload-enable gating, outage-time pending record retention, recovery upload, DAP-reset RTC-derived timestamps, physical OLED/LED/buzzer feedback, and upper-computer import/export against live board records. Upper-computer SQLite and CSV persistence are host-validated for people, issue logs, lost-card marks, attendance import, upload-state refresh and UTF-8-SIG export.

## Implemented

- `pc_tool/`: Python/Tkinter upper-computer for serial communication, card issuing, image blocks, SQLite records, attendance import and weather cache diagnostics.
- `firmware/stm32/NFCAttend_Base/`: STM32 HAL/FreeRTOS base copied from `Demo_W25Q128`.
- `firmware/stm32/Bsp/`: BSP drivers for W25Q128, RC522, OLED, ESP01S, UART, Key, LED and related modules.
- `firmware/third_party/littlefs/`: LittleFS source.
- `firmware/app/`: CRC16, LittleFS storage, serial protocol, UID consistency card operations, network upload, weather cache, OLED display state and application entry modules.
- `server/`: stage-3 TCP upload receiver plus Web backend for device, record and blacklist management.
- `docs/requirements.md`: formal project requirements.
- `docs/reference-materials.md`: reference-material traceability and repository inclusion notes.
- FreeRTOS tasks now initialize the attendance app, dispatch USART1 protocol lines, poll RC522 for local attendance records, refresh OLED display pages, initialize ESP01S on USART6, periodically schedule NTP status checks, weather queries, heartbeat/upload attempts, and dispatch ESP01S TCP ACK lines to the network layer.
- Firmware now writes and reads the card account block at Mifare sector 0 block 1 with UID, SID, points, card type and CRC16.
- Firmware now handles image-card block commands `IMGAxx`, `IMGNxx`, `IMGDxx` and `UPDATEIMG`, writing only Mifare data blocks and requiring a complete same-UID image update session.
- Local NFC attendance uses the validated card account SID instead of UID-only records.
- Local NFC attendance now honors persistent work mode: normal, check-in, check-out and alternating in/out. Check-in rejects cards that already have an unmatched IN record; check-out rejects cards without an unmatched IN record; alternating mode records IN then OUT then IN for the same UID.
- OUT attendance records now calculate in-place duration from the latest matching IN timestamp and report it as `DUR=<seconds>` on the `ATTEND:OK` line; unsigned second subtraction supports cross-day intervals.
- Attendance records are stored in W25Q128/LittleFS as a CRC-checked ring buffer with `records.meta`; default capacity is 1024 records. Once full, new records overwrite the oldest logical record without changing the serial `LIST` order. The app derives the next sequence number from the newest retained record after reboot.
- The local attendance decision path keeps an 8-entry UID LRU cache for latest IN/OUT records, so repeated high-frequency swipes of recent UIDs avoid rescanning Flash.
- `LIST:<count>` and `LIST:ALL` now stream stored attendance records as `REC:` lines after `LIST:COUNT`, including upload state as `UP=PENDING/DONE/FAILED`.
- `SIMATT:<uid>,<sid>,<type>` can inject a simulated attendance record through the app layer while RC522 is paused, now using the same anti-repeat state as local NFC polling.
- `UITEST:<case>` can trigger sparse 24px OLED pages and local LED/buzzer feedback paths while RC522 is paused.
- `WEATHER?`, `WEATHERTEST:<text>`, `WEATHER!`, `TIME?` and `NET?` provide board-verification access to cached weather readback, test-cache writes, forced real ESP01S weather queries, app time-source state and ESP01S runtime link state.
- `CARDLOCK:ON/OFF/?` pauses automatic attendance polling while the upper-computer is reading, issuing, clearing or transferring image blocks; the PC tool wraps every serial job with that lock to prevent asynchronous `ATTEND:*` lines from interrupting command responses.
- The upper-computer serial client now treats `UID:`, `WEATHER:`, `TIME:`, `CARDLOCK:` and `OK:*` replies as transaction terminators, so `READ`, weather/time commands, `ISSUE`, image writes and clear commands do not wait for avoidable timeouts.
- Administrator cards use account `card_type=2`. A valid administrator card enters an on-device administrator mode for 120 seconds; normal cards are rejected without writing attendance while that mode is active. K2/K3 switch between device-id and work-mode fields, K4/K5 decrement/increment values, K1 exits, and K6 saves `/config.bin` then resets so the new device id or mode is effective after reboot.
- Firmware now links the OLED BSP and an `att_display` module; the display task shows device ID, record count, upload enable state, network state, weather placeholder, standby prompt and attendance detail pages. Attendance detail uses the demo-compatible `SimSun_24` font as two 24px pages: page 1 shows time/type and SID, page 2 shows status and result or OUT duration. K3 advances the detail page and K2 returns to the previous page while a detail event is active; detail pages stay visible for 30 seconds.
- ESP01S startup can write NTP-local time into STM32 RTC; the STM32 app time source normalizes the configured local RTC calendar back to UTC Unix seconds before attendance records, `TIME?` and OLED timezone formatting use it. If RTC is not valid, the app falls back to RTOS uptime.
- Firmware now stores latest weather text in LittleFS `/weather.txt` and reloads it into the OLED display model at app startup.
- Firmware feedback events now route attendance/network results to a FreeRTOS queue; L1/L2/L3/L4/L5 and TIM3_CH1 buzzer patterns indicate OK, invalid card, duplicate, fault and network-online states.
- The upper-computer now has a device config tab that sends segmented `CFG:` commands for device id, work mode, upload enable, anti-repeat interval, WiFi, server, weather and timezone; firmware saves those fields into `/config.bin` and reapplies display/network runtime state.
- The upper-computer device config tab can also read cached weather, write a short test weather cache string, force one real ESP01S weather query, read the current device time source and query the `NET?` ESP01S link snapshot.
- `UPLOAD=0` disables heartbeat and pending-record upload attempts while keeping NTP time sync and weather cache/display polling active when the ESP01S network path is ready.
- The upper-computer stores board `UP=PENDING/DONE/FAILED` state in SQLite, displays it in the attendance-record table and exports it to CSV; repeated imports of the same `DEV+SEQ` refresh status without duplicating rows.
- `server/server.py` now runs TCP and HTTP in one process. It stores heartbeats, devices, uploads and blacklist rows in SQLite, exposes `/devices`, `/records`, `/blacklist`, `/ota`, JSON APIs and OTA package metadata, supports `UPLOADB` batch upload ACKs, `BL?` blacklist download, heartbeat online detection and CRC-wrapped TCP frames.
- Firmware batches up to three pending uploads, verifies CRC-wrapped server responses, refreshes the blacklist after heartbeat and locally rejects matching UIDs before attendance record append.
- OTA download and bootloader IAP install are implemented. The prior single-slot path is board-validated. The current A/B path keeps the 64K bootloader, adds slot A at `0x08010000..0x0803FFFF` and slot B at `0x08040000..0x0807FFFF`, stores `ota.bin`/`ota.meta` plus `bootstate.bin` in W25Q128 LittleFS, requests the opposite slot with `OTA?SLOT=A/B`, installs only the target slot, and uses pending/confirmed slot state for rollback.

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- Server host tests cover heartbeat, batch upload, CRC-wrapped upload, blacklist query persistence and OTA package query/chunk responses.
- Upper-computer tests cover people upsert, issue-log insert, lost-card flag updates, administrator-card issue command construction, `REC:` upload-state parsing, duplicate `DEV+SEQ` refresh from `PENDING` to `DONE`, SQLite schema migration and UTF-8-SIG CSV export.
- `pc_tool.nfc_attendance_tool.SerialClient` was validated against the live board on `COM3` for `CFG?`, `TIME?`, `WEATHER?`, `WEATHERTEST`, `WEATHER!` and `LIST:2`.
- `python firmware/app/tests/run_host_tests.py` builds and runs the native C host test executables for protocol routing, USART line buffering, local NFC polling, ESP01S network parsing, CRC-wrapped network ACKs, batch upload ACKs, blacklist refresh, OTA download/cache verification and network polling schedule.
- Runtime server smoke on 2026-07-03 passed with `server.py --host 0.0.0.0 --port 9000 --web-port 8080`: `/` and `/records` returned HTTP 200, `/blacklist/add` accepted a test UID, CRC-wrapped `HEARTBEAT`, `UPLOADB` and `BL?` returned valid CRC-wrapped responses, `/api/devices` showed the smoke device online and `/api/records?dev=909&sid=7001` returned the uploaded record.
- `python tools/run_verification.py` is the default host verification entrypoint; `--firmware-build` adds STM32 clean build and ELF segment permission checks.
- Native NFC host tests cover first IN, duplicate skip, OUT after the repeat interval, alternating back to IN, check-in logical duplicate rejection, check-out no-entry rejection, cross-day OUT duration, latest-record LRU cache hits, sequence continuation from the newest stored record after init, administrator-card entry, normal-card rejection during administrator mode, administrator config save and 120-second timeout.
- Native network host tests cover `NET?`/`att_network_get_status()` reporting configured state, upload enable, ESP01S state, NTP state, blacklist count, device id, server host/port, SSID, OTA query/chunk download state and wrong-slot OTA advertisement rejection.
- Native storage host tests run `att_storage.c` against real littlefs on an in-memory flash block device and cover config roundtrip, CRC-checked ring overwrite of the oldest record when full, logical read order, pending-upload scan, `mark_uploaded()`, OTA cache CRC32 verification and bootstate persistence.
- Native display host tests cover the OLED status model text for standby, network state, weather, two-page `SimSun_24` attendance detail pages, duplicate/invalid/error pages, OUT duration, K2/K3 page switching, UTC+8 display formatting, administrator pages and event timeout behavior.
- ARM GCC compile-only checks also passed for the same protocol, serial, NFC and network test sources.
- `make clean; make` passed in `firmware/stm32/NFCAttend_Base` with LittleFS, RC522, ESP01S, OLED, RTC, LED and MIDI buzzer app modules linked and no warning lines in the build log.
- `arm-none-eabi-readelf -l build/Demo_W25Q128.elf` shows the Flash `PT_LOAD` segment as `R E` and RAM `PT_LOAD` segments as `RW`, with no `RWE`/`RWX` load segment.
- `make APP_OFFSET=1 -j4` passed in `firmware/stm32/NFCAttend_Base`; `build_app/Demo_W25Q128_App.elf` links at `0x08010000`, has `R E` Flash and `RW` RAM load segments, and produces the slot-A-compatible OTA app binary.
- A/B build verification passed in `firmware/stm32/NFCAttend_Base`: `make APP_SLOT=A -j4` produced `build_app_a/Demo_W25Q128_AppA.elf` with vector table `0x08010000`, reset handler `0x0802436C`, `text=117376`, `data=496`, `bss=46600`, and `bin=117884`; `make APP_SLOT=B -j4` produced `build_app_b/Demo_W25Q128_AppB.elf` with vector table `0x08040000`, reset handler `0x0805436C`, `text=117376`, `data=496`, `bss=46600`, and `bin=117884`. Both fit their slots.
- `make clean; make -j4` passed in `firmware/stm32/NFCAttend_Bootloader`; `NFCAttend_Bootloader.elf` uses `text=30016`, `data=152`, `bss=2640`, `bin=30176`, fits the 64K bootloader partition, and `readelf` shows Flash `R E` plus RAM `RW` load segments.
- STM32 direct-flash firmware size after the A/B support update: `text=117344`, `data=496`, `bss=46600`, `bin=117852`.

## Board Validated

- CMSIS-DAP flashing and verify/reset.
- Complete firmware flashed and verified after the `CARDLOCK` update, after the work-mode/OLED-detail update, after the `SimSun_24` K2/K3 two-page OLED detail update, after the administrator-mode update, after the Flash ring-buffer/LRU update, after the Web/CRC/batch/blacklist network update and after the stage-1 OTA update.
- USART1 over `COM3` responds to `PING`, `CFG?`, `LIST:1`, `LIST:ALL`, `OLEDTEST` and `UITEST:OK` after repeated commands.
- `CARDLOCK?`, `CARDLOCK:ON`, `CARDLOCK:OFF` were validated over `COM3`; paused mode prevents automatic NFC attendance polling while serial jobs run.
- RC522 diagnostic on the complete firmware returns `RC522_RAW=0x92`, `RC522_VER=0x92`, `RW=1`; the latest run had no card on the antenna (`REQ=-1`, `READ -> ERR:NO_CARD`).
- Earlier same-branch complete-firmware board runs read UID `83A40792`, issued it as SID `1001`, appended `ATTEND:OK:SEQ=12`, and returned that record through `LIST:5`.
- W25Q128/LittleFS mounts and loads persistent `/config.bin`.
- Segmented `CFG:` writes for device/mode/upload/repeat/timezone/WiFi/server/weather fields persist across CMSIS-DAP reset and can be restored to production values.
- Empty board storage returns `LIST:COUNT=0` and `LIST:END`; simulated records are appended and read back through the CRC-checked record path.
- Board W25Q128 persistence after the Flash ring-buffer/LRU update: two `SIMATT` records appended as `SEQ=104` and `SEQ=105`, `LIST:2` returned both records, DAP reset preserved `LIST:COUNT=103` and both latest records.
- Board config persistence after the Flash ring-buffer/LRU update: `CFG:DEV=77|MODE=2` read back immediately and after DAP reset; config was restored to `DEV=1|MODE=3` and confirmed again after reset.
- OLED initializes and displays the sparse 24px `OLEDTEST` and weather pages.
- `WEATHERTEST:Sunny 20C` creates/updates `/weather.txt`; `WEATHER?` reads it back as `WEATHER:Sunny 20C` after DAP reset.
- ESP01S connects WiFi, syncs NTP into RTC, connects TCP to `server/server.py` and sends heartbeat.
- After a DAP reset, a pre-NTP simulated attendance record used RTC-derived Unix seconds instead of RTOS uptime fallback.
- `TIME?` reports the app time source over `COM3`; after the administrator-mode update, `TIME:1783007666|VALID=1` converted to `2026-07-02 23:54:26` at UTC+8 while `CFG?` reported `DEV=1|MODE=3|TZ=8`.
- Weather location is configured for Hangzhou as `WLOC=30.267:120.153`; a private Seniverse `WKEY` was validated locally, and board `WEATHER!` returned `WEATHER:Light 23C`.
- Simulated attendance upload reaches `server/server.py`, receives `ACK:UPLOAD:<seq>` and persists `UP=DONE` in LittleFS; with the server endpoint unavailable, a simulated record remains `UP=PENDING` and later changes to `UP=DONE` after restoring the endpoint.
- `server/server.py` now writes valid `UPLOAD:` packets into `server/data/attendance.db` table `uploads`, deduplicated by `(DEV, SEQ)`, while preserving the legacy `uploads.log`.
- Network polling now sends pending uploads before weather queries. If an upload frame is sent, weather is deferred; host tests cover upload-priority scheduling, weather-failure isolation and pending-upload retry after app reinit.
- Board networking regression on 2026-07-03: after flashing the final firmware, `CFG?` reported `UPLOAD=1|HOST=192.168.107.234|PORT=9000|TZ=8`, `TIME?` returned a valid RTC-derived Unix timestamp, TCP stayed established, and `server/data/attendance.db` contained 18 deduplicated upload rows. The board resumed a previously stuck backlog from `SEQ=78` through `SEQ=97`; `uploads.log` and SQLite both showed the recovered sequence progression.
- The recovered backlog exposed two storage edge cases that are now fixed: ACK parsing accepts prefixed `+IPD,...ACK:UPLOAD:<seq>` lines, and `att_storage_mark_uploaded()` skips CRC-bad historical records while marking every valid duplicate sequence.
- `SIMATT` now rejects same-UID attendance inside `REPEAT=60` with `ERR:DUPLICATE`; `UPLOAD=0` keeps new simulated records local as `UP=PENDING`, and restoring `UPLOAD=1` retries them to `UP=DONE`.
- Board networking regression after the Web/CRC/batch/blacklist update: CMSIS-DAP `program build/Demo_W25Q128.elf verify reset exit` succeeded, `PING -> OK:PONG`, `CFG?` reported `UPLOAD=1|HOST=192.168.107.234|PORT=9000|TZ=8`, `TIME?` returned `VALID=1`, and `LIST:1` preserved Flash record `SEQ=108`.
- The flashed board sends CRC-wrapped heartbeat and blacklist refresh frames: `CRC:HEARTBEAT:DEV=1|TEMP=0*BF13` and `CRC:BL?*304C`; the Web API shows device `DEV=1` online.
- Board CRC upload verification after flash: `SIMATT:01020309,7901,2` created `SEQ=109`, then `LIST:1` changed from `UP=PENDING` to `UP=DONE`, and the server stored `CRC:UPLOAD:SEQ=109|UID=01020309|SID=7901|TYPE=2|TS=1783048876|DEV=1*773D`.
- Board batch upload verification after flash: with `UPLOAD=0`, `SIMATT` created `SEQ=110` and `SEQ=111` as pending; after restoring `UPLOAD=1`, both records changed to `UP=DONE`, the Web API showed SIDs `7902` and `7903`, and the server log contained `CRC:UPLOADB:DEV=1|R=110,01020310,7902,2,1783048975;111,01020311,7903,2,1783048975*4D69`.
- DAP reset after the Web/CRC/batch/blacklist update preserved config and records: `PING -> OK:PONG`, `CFG?` still reported `UPLOAD=1|HOST=192.168.107.234|PORT=9000`, `LIST:2` returned `SEQ=110/111` as `UP=DONE`, `TIME?` remained `VALID=1`, and the server API showed `DEV=1` online again.
- Realtime temperature now uses the STM32F407 internal temperature sensor. Board heartbeat after flashing reported `CRC:HEARTBEAT:DEV=1|TEMP=40.2*B842`, and the Web API showed `temperature_c=40.2`.
- Board firmware with the `NET?` diagnostic was flashed and verified on 2026-07-03. On a requested router network, `CFG?` read back `HOST=192.168.5.94|PORT=9000|SSID=<test-router-ssid>`, while `NET?` returned `READY=0|CFG=1|UPLOAD=1|STATE=2|TEXT=WIFI_CONNECTING|NTP=0|BL=0`; the server was listening on `0.0.0.0:9000/8080` but `/api/devices` still showed the last device heartbeat from old peer `192.168.107.122`.
- Windows WLAN scan during the same run showed the requested router SSID only as `5 GHz` and `WPA3-Personal`; no 2.4 GHz/WPA/WPA2 BSSID was visible. This explains the ESP01S staying in `WIFI_CONNECTING`; it needs a 2.4 GHz WPA/WPA2-compatible SSID for network upload validation.
- Switching development back to a 2.4 GHz phone hotspot on 2026-07-03 restored ESP01S networking. The PC WLAN IP was `192.168.107.234`; after `CFG:SSID=<test-wifi-ssid>`, password update and `CFG:HOST=192.168.107.234|PORT=9000`, DAP reset led to `NET:READY=1|STATE=6|TEXT=TRANSPARENT|NTP=1|BL=3`. Backlog record `SEQ=115` changed from `UP=PENDING` to `UP=DONE`, and a fresh `SIMATT:0A0B0C01,8801,2` created `SEQ=116`, uploaded it to the server and changed it to `UP=DONE`.
- Stage-1 OTA board validation on 2026-07-03 first used server package `stage1-test` (`SIZE=8`, `CRC32=C4ABFFF5`) over a 2.4 GHz hotspot. The board reached `NET:READY=1|STATE=6|TEXT=TRANSPARENT`, `OTA!` caused the server to log `CRC:OTA?*45FD` and `CRC:OTA:GET:OFFSET=0|LEN=8*D2E3`, and `OTA?` returned `OTA:STATE=READY|VER=stage1-test|RX=8|SIZE=8|CRC32=C4ABFFF5|ACT=C4ABFFF5`. After DAP reset, `OTA?` returned `OTA:STATE=CACHED|OK=1|VER=stage1-test|RX=8|SIZE=8|CRC32=C4ABFFF5|ACT=C4ABFFF5`, proving W25Q128 persistence.
- Full firmware-package OTA cache validation then used `server/data/ota/current.bin` copied from the direct-flash `build/Demo_W25Q128.bin` with `VER=fw-20260703-192`, `SIZE=116668`, `CRC32=25A687EF` and `CHUNK=192`. The board downloaded the full package to W25Q128 and reported `OTA:STATE=READY|VER=fw-20260703-192|RX=116668|SIZE=116668|CRC32=25A687EF|ACT=25A687EF`; after DAP reset it reported `OTA:STATE=CACHED|OK=1|VER=fw-20260703-192|RX=116668|SIZE=116668|CRC32=25A687EF|ACT=25A687EF`. Future bootloader-install tests must use `build_app/Demo_W25Q128_App.bin`, not the direct-flash binary.
- Bootloader/IAP board validation on 2026-07-03 flashed `NFCAttend_Bootloader.elf` plus offset-linked `Demo_W25Q128_App.elf`. The first bootloader jump exposed an MSP boundary check bug because the valid initial stack pointer was exactly `0x20020000`; `boot_main.c` now accepts aligned stack pointers `> SRAM_START` and `<= SRAM_END`. After the fix, reset/halt showed PC `0x08022980`, inside the app slot, and the app responded on `COM3` with `PING -> OK:PONG`, `CFG?` and `OTA? -> OTA:STATE=IDLE`.
- Full bootloader OTA install then used `server/data/ota/current.bin` copied from `build_app/Demo_W25Q128_App.bin` with `SIZE=117156`, `CRC32=382E2334`, `VER=app-boot-202607` and `CHUNK=192`. The board downloaded the complete image and reported `OTA:STATE=READY|VER=app-boot-202607|RX=117156|SIZE=117156|CRC32=382E2334|ACT=382E2334`; `OTARST` returned `OK:OTARST`; after reset and bootloader install, `PING -> OK:PONG` and `OTA? -> OTA:STATE=CACHED|OK=1|VER=app-boot-202607|RX=117156|SIZE=117156|CRC32=382E2334|ACT=382E2334|TARGET=08010000|INSTALL=3|ERR=0`.
- A/B OTA board validation passed on 2026-07-03. The board started in slot A, downloaded `current_B.bin` as `OTA?SLOT=B` with `SIZE=117884`, `CRC32=8F4F3162` and `ACT=8F4F3162`, accepted `OTARST`, installed through the bootloader and rebooted as `CUR=B|TARGET=08040000|INSTALL=3|ERR=0`. It then downloaded `current_A.bin` as `OTA?SLOT=A` with `SIZE=117884`, `CRC32=872089CC` and `ACT=872089CC`, accepted `OTARST`, installed through the bootloader and rebooted as `CUR=A|TARGET=08010000|INSTALL=3|ERR=0`. After the second reboot, `NET?` recovered to `READY=1|STATE=6|TEXT=TRANSPARENT|NTP=1`.
- CRC-framed `$PING*6427` returns `OK:PONG`; a bad CRC frame returns `ERR:CRC`; bad `LIST` arguments return `ERR:ARG`; no-card `READ`/`ISSUE`/`CLEAR` return `ERR:NO_CARD`.
- Image command boundary checks reject `IMGN10` and bad 16-byte hex payloads with `ERR:ARG`; incomplete `UPDATEIMG` returns `ERR:NOT_READY`.
- `UITEST:READY/OK/DUP/INVALID/ERROR/NETOK/NETERR` commands are accepted over `COM3` and route through the same display/feedback callbacks as runtime events.
- Physical observation confirmed the sparse 24px OLED pages, L1-L5 LED mapping and TIM3_CH1 PB4 buzzer feedback for the simulated `UITEST` cases.
- The upper-computer `SerialClient` read live `LIST:5` records from `COM3`, imported five `REC:` rows with `UP=DONE` into a temporary SQLite database and exported a CSV with the `upload_state` column.

## Not Yet Hardware Validated

- Real-card local blacklist rejection after server `BL?` refresh still needs a physical card-present test.
- Physical long-run fill to 1024 real board records and observe the next write overwriting the oldest record; the ring behavior is covered by native littlefs storage tests with a reduced capacity.
- ESP01S real RC522-driven attendance for the latest records after backlog drains; simulated/backlog records now have `UPLOAD:` plus `ACK:UPLOAD:<seq>` plus SQLite-row proof.
- Temperature is board-validated through the STM32F407 internal temperature sensor; an external ambient sensor is not required for the current demo.
- Strict RTC retention timing proof before fresh NTP sync after full power-loss/VBAT conditions.
- Physical observation of LED and TIM3_CH1 PB4 feedback after real RC522 card events.
- Physical observation of the post-flash `UITEST:OK` two-page OLED screen after pressing K3/K2.

## Main Risks

- Some original BSP comments are mojibake, but the C interfaces are usable.
- Network ACK parsing and `att_storage_mark_uploaded()` are board-validated with an injected attendance record; real RC522-driven upload still needs a card-present regression run.
- The current tested router AP configuration is not suitable for ESP01S validation because only 5 GHz WPA3 is visible. Use a 2.4 GHz WPA/WPA2 SSID, or enable mixed 2.4 GHz compatibility on this router, before judging firmware/server upload behavior on that network.
- RC522 communication is healthy, but card reads remain sensitive to card placement; the latest post-flash check saw no card on the antenna.
- LittleFS currently uses the whole W25Q128. If raw Flash areas are needed later, the volume must be partitioned or offset.
- Bootloader install currently reuses the board-validated W25Q128 LittleFS volume. OTA throughput is slow because the verified chunk size is 192 bytes; increasing it requires checking the ESP01S UART RX path and hex-encoded line length.
