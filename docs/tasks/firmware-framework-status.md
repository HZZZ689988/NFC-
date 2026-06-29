---
type: task-status
task: firmware-framework
implemented: yes
hardware-validated: no
done: no
evidence-level: host-build
updated: 2026-06-29
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
- STM32 base now links LittleFS plus `att_crc16`, `att_lfs_port`, `att_storage` and `att_protocol`.
- The FreeRTOS LED demo task now initializes W25Q128, mounts/formats LittleFS through `att_storage_init`, loads `config.bin`, and prints the storage state over USART1.
- `att_storage_init` is idempotent after a successful mount, so the task can re-run the bootstrap status path without remounting an already mounted filesystem.
- The K3 key now prints a LittleFS storage status line with record count, device id and upload-enable state for board-side smoke testing.

## Not Done

- RC522 and ESP01S application modules are not yet linked into the STM32 base Makefile.
- FreeRTOS tasks do not yet run card issuing, record creation, serial protocol dispatch or ESP01S upload logic.
- No real-board validation has been performed in this repository.
