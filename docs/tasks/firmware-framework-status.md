---
type: task-status
task: firmware-framework
implemented: yes
hardware-validated: no
done: no
evidence-level: unit-test
updated: 2026-06-30
---

# Firmware Framework Status

## Implemented

- STM32 base project copied from `Demo_W25Q128`.
- BSP drivers copied from provided materials.
- LittleFS source added.
- Application modules added:
  - `attendance_app`
  - `att_storage`
  - `att_lfs_port`
  - `att_protocol`
  - `att_card`
  - `att_network`
  - `att_crc16`

## Host Verification

- STM32 base builds with `make`.
- Application-layer C files compile as ARM Cortex-M4 objects with the STM32/BSP include paths.
- STM32 base now links LittleFS, RC522 BSP/platform support, ESP01S BSP support and the local attendance app modules: `attendance_app`, `att_card`, `att_crc16`, `att_lfs_port`, `att_storage`, `att_protocol` and `att_network`.
- The FreeRTOS LED demo task now initializes W25Q128, mounts/formats LittleFS through `att_storage_init`, loads `config.bin`, and prints the storage state over USART1.
- `att_storage_init` is idempotent after a successful mount, so the task can re-run the bootstrap status path without remounting an already mounted filesystem.
- The K3 key now prints a LittleFS storage status line with record count, device id and upload-enable state for board-side smoke testing.
- The FreeRTOS startup path now calls `attendance_app_init()`, which initializes storage and RC522 app support before reporting readiness over USART1.
- The FreeRTOS runtime now creates USART1 serial, NFC polling and ESP01S network tasks.
- ESP01S uses USART6, is initialized before `attendance_app_init()` writes stored config into the network layer, and starts WiFi/TCP in its own low-priority task so local attendance is not blocked.
- `attendance_app_poll_network()` schedules heartbeat every 60 seconds and pending-upload attempts every 10 seconds when upload is enabled.
- ESP01S transparent TCP data is dispatched through a FreeRTOS queue to `att_network_handle_rx()`, which marks records uploaded only after parsing `ACK:UPLOAD:<seq>`.
- Host tests cover protocol routing, USART line buffering, NFC attendance polling, network config bounds, ACK parsing and network polling schedule.
- `att_card_issue_checked()` writes UID, SID, points, card type and CRC16 to Mifare sector 0 block 1.
- `att_card_read_person()` reads the same account block, verifies the physical UID against the stored UID and validates CRC16 before exposing person data.
- Local NFC polling appends attendance records with the card account SID and rejects CRC/UID-invalid cards without appending records.
- `LIST:N` and `LIST:ALL` stream attendance records over serial as `REC:` lines after `LIST:COUNT`.

## Not Done

- RC522 UID read and card block read/write are build-linked but not real-board validated.
- Card account block read/write and CRC invalid-card handling are implemented but not real-board validated.
- `LIST:N` / `LIST:ALL` record streaming is build-checked but not validated through USART1 against real board storage.
- ESP01S WiFi, TCP, NTP, weather, heartbeat and upload paths are build-linked/scheduled but not real-board validated.
- Upload ACK parsing and `att_storage_mark_uploaded()` integration are host-tested but not real-board validated.
- Native C host test executables were not run on 2026-06-30 because `gcc`, `clang`, `cl` and `zig` are not installed in the current environment; ARM GCC compile-only checks were used instead.
- No real-board validation has been performed in this repository.
