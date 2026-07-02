---
type: verification
target: non-rc522-requirements
updated: 2026-07-02
hardware-validated: partial
---

# Non-RC522 Validation Matrix

RC522 real-card I/O is intentionally paused. This matrix records which project
requirements have been validated without RC522 and which still require a real
card path before final acceptance.

## Validated Without RC522

| Requirement area | Substitute / evidence |
| --- | --- |
| DAP download and debug UART | CMSIS-DAP programming plus `COM3` serial command evidence in `docs/evidence/2026-07-01-board-bringup.md`. |
| W25Q128 and LittleFS | `W25Q128 ID: 0xEF17`, LittleFS mount, `/config.bin`, `/records.bin` and `/weather.txt` read/write evidence. |
| Persistent device config | Segmented `CFG:` writes persisted across CMSIS-DAP reset and were restored to production config. |
| USART1 protocol | Legacy and CRC-framed commands validated: `PING`, bad CRC rejection, `CFG?`, `LIST`, `READ`, `ISSUE`, `CLEAR`, `WEATHER?`, `WEATHER!`, `TIME?`, `UITEST`. |
| Attendance record append/read | `SIMATT` injects records through the app/storage layer; `LIST:<count>` streams CRC-checked stored `REC:` rows. |
| Anti-repeat behavior | Duplicate `SIMATT` for the same UID inside `REPEAT=60` returns `ERR:DUPLICATE` and appends no second record. |
| Upload enable gating | `UPLOAD=0` keeps simulated records as `UP=PENDING`; restoring `UPLOAD=1` retries upload. |
| Offline upload recovery | Deliberately unreachable server port kept records pending; restoring the server endpoint changed records to `UP=DONE`. |
| ESP01S networking | WiFi, NTP-to-RTC, TCP server connection, heartbeat and upload ACK were validated on board. |
| Weather | Hangzhou Seniverse `WEATHER!` query returned `WEATHER:Light 23C`; cache persisted in LittleFS and displayed on OLED. |
| RTC after DAP reset | `TIME?` and pre-NTP simulated records used valid Unix seconds immediately after CMSIS-DAP reset. |
| OLED / LED / buzzer | `UITEST` routes through the same display/feedback callbacks as runtime events; physical observation matched expected sparse 24px OLED pages, L1-L5 and buzzer behavior. |
| Upper-computer serial interop | `SerialClient` was validated against live board `CFG?`, `TIME?`, weather commands and `LIST`. |
| Upper-computer SQLite/CSV | People, issue logs, lost-card marks, record import, upload-state refresh, migration and UTF-8-SIG CSV export are host-tested; live `LIST:5` records were imported and exported. |

## Still Requires RC522 Real-Card Validation

| Requirement area | Reason |
| --- | --- |
| UID read | RC522 diagnostics still report `RC522_VER=0x00`; no valid UID has been read from the chip. |
| UID mismatch rejection | Requires one real card UID read and a deliberately different command UID. |
| Account block write/read | Requires working Mifare authentication and block write/read through RC522. |
| Invalid account block CRC rejection | Requires either a corrupt real card block or a card-read mock path extended specifically for that test. |
| Image card write flow | `IMGAxx`, `IMGNxx`, `IMGDxx` and `UPDATEIMG` are host-tested but need real card block writes and readback. |
| Real card attendance event | `SIMATT` covers app/storage/network behavior, but the RC522-triggered polling path must still produce a real record. |
| Real card UI/feedback | `UITEST` covers callbacks and hardware output, but real RC522 events must still drive those callbacks. |
| Full power-loss / VBAT RTC retention | DAP reset and one reported power-cycle gave valid time evidence, but strict VBAT retention before fresh NTP sync remains a separate hardware check. |

## Current Acceptance Boundary

All non-RC522 development and validation requested for this phase is covered by
host tests, board command evidence or upper-computer live import/export evidence.
The project should not be marked fully complete until RC522 communication is
restored and the real-card rows above are validated.
