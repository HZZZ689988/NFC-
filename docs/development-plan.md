# Development Plan

## Stage 1: Upper-Computer And Serial Protocol

- Keep the existing ASCII commands for early testing.
- Add CRC16 frame support: `$PAYLOAD*CRC16`.
- Support UID read, card clear, issuing, image block transfer and attendance import.
- Keep local SQLite records for people, issue logs and imported attendance.

## Stage 2: Firmware Local Attendance

- Integrate `firmware/app` into `firmware/stm32/NFCAttend_Base`.
- Initialize W25Q128, LittleFS, RC522, OLED, keys, buzzer and USART1.
- Store config in `config.bin`.
- Store attendance records in `records.bin`.
- Read card UID, load card account data, generate local attendance record.
- Enforce anti-repeat interval.
- Implement `READ`, `ISSUE`, `CLEAR`, `LIST`, `CFG?` over serial.

## Stage 3: Required Networking

- Initialize ESP01S from stored config.
- Connect WiFi and TCP test server.
- Sync RTC using NTP.
- Query weather and cache/display latest result.
- Upload pending records.
- Retry records after disconnection.
- Send heartbeat periodically.

## Stage 4: UI And Demo Polish

- OLED pages: standby, card result, network status, weather, error state.
- Key menu: device ID, mode, WiFi/server status, storage format.
- Buzzer/LED feedback for success, duplicate, invalid card and network fault.

## Done Criteria

- PC tool can issue a card only when UID matches the physical card.
- Device can record attendance offline and persist after reset.
- Device uploads records after network recovery.
- Server receives `UPLOAD` and `HEARTBEAT` messages.
- Hardware validation notes are recorded under `docs/verification`.
