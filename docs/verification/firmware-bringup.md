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
- [ ] Add RC522 card and ESP01S network modules to linked firmware after GPIO/UART6 task integration.

## Hardware

- [ ] W25Q128 ID reads as `0xEF17`.
- [ ] LittleFS formats and mounts.
- [ ] `config.bin` defaults are created.
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
