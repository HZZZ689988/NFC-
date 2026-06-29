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

## Not Done

- RC522 and ESP01S application modules are not yet linked into the STM32 base Makefile.
- FreeRTOS tasks do not yet call the application layer.
- No real-board validation has been performed in this repository.
