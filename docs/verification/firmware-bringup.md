---
type: verification
target: firmware
hardware-validated: partial
updated: 2026-07-02
---

# Firmware Bring-Up Checklist

## Build

- [x] Build `firmware/stm32/NFCAttend_Base` without app layer.
- [x] Add LittleFS, storage and protocol core entries to Makefile.
- [x] Compile app modules as standalone ARM objects.
- [x] Build linked firmware with LittleFS, storage and protocol core modules.
- [x] Call W25Q128 plus LittleFS storage bootstrap from the FreeRTOS demo task.
- [x] Add RC522 card modules to linked firmware.
- [x] Add USART1 serial line dispatch task and low-frequency NFC polling task.
- [x] Add ESP01S network modules to linked firmware after UART6 task integration.
- [x] Enable `ATT_ENABLE_NETWORK` in the STM32 Makefile and build the network task path.
- [x] Add card account read path and serial record streaming path to linked firmware.
- [x] Add image-card block command handling and same-UID image update session tracking to linked firmware.
- [x] Add OLED BSP, `att_display` and FreeRTOS display task to linked firmware.
- [x] Add RTC BSP, NTP-to-RTC write path and `/weather.txt` weather cache path to linked firmware.
- [x] Add LED BSP, MIDI buzzer BSP and FreeRTOS local feedback routing to linked firmware.
- [x] Add `CFG:` device/network config writes to firmware and upper-computer segmented config commands.

## Host Verification

- [x] `firmware/app/tests/test_att_protocol_host.c` covers card command routing.
- [x] `firmware/app/tests/test_attendance_serial_host.c` covers USART line buffering.
- [x] `firmware/app/tests/test_attendance_nfc_host.c` covers local NFC record append, duplicate skipping, no-card silence and storage error logging.
- [x] `firmware/app/tests/test_att_network_host.c` covers ESP01S config bounds, network config copying and upload ACK parsing.
- [x] `firmware/app/tests/test_attendance_network_host.c` covers heartbeat/upload polling schedule and confirms polling does not mark uploads done without ACK.
- [x] `python -m compileall pc_tool server` passes.
- [x] `python pc_tool/tests/test_core.py` passes, including server `ACK:UPLOAD:<seq>` compatibility.
- [x] `make clean; make` passes in `firmware/stm32/NFCAttend_Base`.
- [x] Resolve known W25QXX build warning: unused local variable `temp` in `W25QXX_Init`.
- [x] ARM GCC compile-only checks pass for `test_att_protocol_host.c`, `test_attendance_serial_host.c`, `test_attendance_nfc_host.c`, `test_att_network_host.c` and `test_attendance_network_host.c`.
- [x] `LIST:N` logic has compile-checked tests for streaming newest records and rejecting invalid list counts before sending partial list output.
- [x] `IMGAxx`, `IMGNxx`, `IMGDxx` and `UPDATEIMG` protocol routing has compile-checked tests.
- [x] Python serial-client tests cover `UID:` and `OK:*` transaction terminators.
- [x] `att_display.c` and `attendance_app.c` compile as ARM Cortex-M4 objects with display calls linked.
- [x] `firmware/app/tests/test_att_display_host.c` covers the display status model with a host GUI stub.
- [x] `make clean; make` links OLED BSP sources: `ssd1306.c`, `ssd1306_i2c.c`, `GUISlim.c` and `F08_ASCII.c`.
- [x] `att_storage.c`, `att_network.c`, network/weather polling tests and full STM32 firmware compile with weather cache and RTC integration.
- [x] Host tests verify `UPLOAD=0` skips heartbeat/upload while preserving NTP and weather polling.
- [x] Feedback callback paths and full STM32 firmware compile with LED/MIDI buzzer integration.
- [x] Protocol and Python tests cover `CFG:` config command construction, persistence and runtime apply callback paths.
- [x] Resolve linker warning: `build/Demo_W25Q128.elf has a LOAD segment with RWX permissions`.
- [x] Clean inherited OLED `GUISlim.c` `-Wmisleading-indentation` warnings.
- [x] Native C host test executables run on a machine with `gcc`, `clang`, `cl` or `zig`.
- [x] `python firmware/app/tests/run_host_tests.py` builds and runs all native C host test executables from a temporary directory.
- [x] `python firmware/app/tests/test_run_host_tests_py.py` covers host runner command generation, including MSVC object output placement.
- [x] `python tools/run_verification.py` runs Python compile checks, upper-computer/server tests, native C host tests and `git diff --check`.
- [x] `python tools/run_verification.py --firmware-build` additionally runs STM32 clean build and ELF segment permission checks.
- [x] `DIAG?` protocol routing is host-tested for RC522 register diagnostic output.

## Hardware

- [x] W25Q128 ID reads as `0xEF17`.
- [x] LittleFS formats and mounts on the board.
- [x] `config.bin` defaults are created on the board.
- [x] `records.bin` append/read passes CRC16 through simulated attendance append and `LIST` readback.
- [x] RC522 diagnostic command runs over `COM3`.
- [x] Compare current RC522 driver against `BSP.rar` / `Demo_RC522`.
- [x] RC522 platform init reconfigures the selected RC522 GPIOs after peripheral init.
- [ ] RC522 register read returns a valid MFRC522 version value.
- [ ] RC522 reads UID.
- [ ] `ISSUE` rejects no-card.
- [ ] `ISSUE` rejects mismatched UID.
- [ ] `ISSUE` writes matching card.
- [ ] Issued card account block can be read back with matching UID, SID, points, card type and CRC16.
- [ ] Invalid account block CRC is rejected without appending an attendance record.
- [ ] `IMGA00..23`, `IMGN00..09`, `IMGD00..09` write to a real image card and `UPDATEIMG` returns success.
- [ ] `UPDATEIMG` returns `ERR:NOT_READY` when any image block is missing.
- [ ] Image-card block contents can be read back from RC522 and match upper-computer generated bitmap data.
- [x] USART1 responds to `PING` and `CFG?`.
- [x] USART1 accepts segmented `CFG:` writes and persists `/config.bin` on W25Q128/LittleFS.
- [x] `CFG:` WiFi/server/weather/timezone changes are reflected in ESP01S startup behavior after config write.
- [x] USART1 `LIST:1` and `LIST:ALL` return `LIST:COUNT=0` and `LIST:END` on empty board storage.
- [x] USART1 `LIST:<count>` streams stored `REC:` lines after simulated attendance records exist.
- [x] ESP01S connects WiFi.
- [x] NTP sync updates RTC.
- [x] RTC-derived attendance timestamps use valid Unix seconds immediately after DAP reset and before a fresh NTP sync.
- [ ] RTC keeps valid time across full power-loss/VBAT conditions.
- [x] ESP01S TCP connects to `server/server.py`.
- [x] TCP upload reaches `server/server.py`.
- [x] `ACK:UPLOAD:<seq>` marks the uploaded record as `UP=DONE` in LittleFS.
- [x] Heartbeat reaches `server/server.py`.
- [x] OLED initializes on I2C1 and responds to `OLEDTEST` with the sparse 24px test page.
- [x] USART1 `UITEST:READY/OK/DUP/INVALID/ERROR/NETOK/NETERR` commands trigger OLED/feedback paths and acknowledge over `COM3`.
- [x] OLED displays standby and simulated attendance/network pages through `UITEST`, confirmed by physical observation.
- [ ] OLED displays attendance OK, duplicate, invalid-card and error pages after real RC522 card events.
- [ ] OLED displays ESP01S network state and latest weather text after network/weather integration.
- [ ] LittleFS `/weather.txt` is created/updated after successful weather query and reloaded on reboot.
- [x] L1 indicates simulated attendance OK through `UITEST`, confirmed by physical observation.
- [x] L2 indicates simulated invalid-card rejection through `UITEST`, confirmed by physical observation.
- [x] L3 indicates simulated duplicate attendance skip through `UITEST`, confirmed by physical observation.
- [x] L4 indicates simulated storage/card/network fault through `UITEST`, confirmed by physical observation.
- [x] L5 indicates simulated network-online transition through `UITEST`, confirmed by physical observation.
- [ ] L1-L5 indicate the mapped events after real RC522 card events.
- [x] TIM3_CH1 PB4 buzzer emits the mapped short tones for simulated attendance and network feedback, confirmed by physical observation.
- [ ] TIM3_CH1 PB4 buzzer emits the mapped short tones after real RC522 card events.
- [x] `CFG?` and `LIST:3` still report persisted config and uploaded records after DAP reset.
