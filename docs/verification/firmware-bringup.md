---
type: verification
target: firmware
hardware-validated: no
updated: 2026-06-29
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

## Hardware

- [ ] W25Q128 ID reads as `0xEF17`.
- [ ] LittleFS formats and mounts on the board.
- [ ] `config.bin` defaults are created on the board.
- [ ] `records.bin` append/read passes CRC16.
- [ ] RC522 reads UID.
- [ ] `ISSUE` rejects no-card.
- [ ] `ISSUE` rejects mismatched UID.
- [ ] `ISSUE` writes matching card.
- [ ] USART1 responds to `PING` and `CFG?`.
- [ ] ESP01S connects WiFi.
- [ ] NTP sync updates RTC.
- [ ] TCP upload reaches `server/server.py`.
- [ ] Heartbeat reaches `server/server.py`.
