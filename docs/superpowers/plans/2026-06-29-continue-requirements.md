# Continue Requirements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Continue the NFC attendance project through small firmware and protocol slices that build on the existing application modules.

**Architecture:** Keep `attendance_app` as the facade and wire it into STM32 FreeRTOS tasks. Extend existing `att_protocol`, `att_card`, `att_storage` and `att_network` APIs only where the next slice needs behavior that can be host-built or firmware-built.

**Tech Stack:** STM32F407 HAL/FreeRTOS C firmware, RC522/W25Q128/ESP01S BSP drivers, LittleFS, Python PC tool and TCP test server.

---

## File Structure

- `firmware/stm32/NFCAttend_Base/Makefile`: include app modules and BSP modules as they become active.
- `firmware/stm32/NFCAttend_Base/Core/Src/freertos.c`: create and run attendance polling tasks using existing app facade functions.
- `firmware/app/Inc/attendance_app.h` and `firmware/app/Src/attendance_app.c`: expose status helpers and route card/protocol/network polling.
- `firmware/app/Inc/att_protocol.h` and `firmware/app/Src/att_protocol.c`: implement command behavior and response strings.
- `firmware/app/Inc/att_card.h` and `firmware/app/Src/att_card.c`: expose UID/account read/issue/clear operations.
- `firmware/app/Inc/att_network.h` and `firmware/app/Src/att_network.c`: keep upload/heartbeat behavior isolated from local attendance.
- `docs/project-status.md`, `docs/tasks/firmware-framework-status.md`, `docs/work-log/2026-06-29.md`: update after each verified firmware slice.

## Task 1: Link Existing Card And App Modules

**Files:**
- Modify: `firmware/stm32/NFCAttend_Base/Makefile`
- Modify: `docs/tasks/firmware-framework-status.md`

- [ ] Add these sources to `C_SOURCES`: `../Bsp/NFC/rc522.c`, `../Bsp/NFC/rc522_platform_stm32.c`, `../../app/Src/att_card.c`, `../../app/Src/attendance_app.c`.
- [ ] Add include path `-I../Bsp/NFC` to `AS_INCLUDES` and `C_INCLUDES`.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Update the task status doc to say RC522/app modules are linked but hardware UID read is not yet validated.
- [ ] Commit with `feat: link firmware card app modules`.

## Task 2: Wire Attendance App Initialization Into FreeRTOS

**Files:**
- Modify: `firmware/stm32/NFCAttend_Base/Core/Src/freertos.c`
- Modify: `docs/tasks/firmware-framework-status.md`

- [ ] Replace the storage-only bootstrap call with `attendance_app_init()` after `W25QXX_Init()`.
- [ ] Keep K3/K6 storage status actions available for board smoke testing.
- [ ] Print the returned status over USART1.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `feat: initialize attendance app task`.

## Task 3: Complete Serial Command Behavior

**Files:**
- Modify: `firmware/app/Src/att_protocol.c`
- Modify: `firmware/app/Inc/att_card.h`
- Modify: `firmware/app/Src/att_card.c`
- Modify: `docs/protocol.md`

- [ ] Add `READ` support that calls `att_card_read_uid()` and returns `UID:XXXXXXXX` or `ERR:NO_CARD`.
- [ ] Change `ISSUE` to build an `att_person_t` and call `att_card_issue_checked()`.
- [ ] Add `CLEAR:UID` support through `att_card_clear_checked()`.
- [ ] Map UID mismatch to `ERR:UID_MISMATCH`.
- [ ] Keep `LIST:N` count/end behavior until record streaming is added.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `feat: handle card serial commands`.

## Task 4: Add Firmware Serial Line Pump

**Files:**
- Modify: `firmware/stm32/NFCAttend_Base/Core/Src/freertos.c`
- Modify: `firmware/app/Inc/attendance_app.h`
- Modify: `firmware/app/Src/attendance_app.c`

- [ ] Add a FreeRTOS serial task or polling path that feeds complete USART1 lines into `att_protocol_handle_line()`.
- [ ] Use a small bounded line buffer and discard overlong lines with `ERR:ARG`.
- [ ] Send responses through the existing `printf`/USART debug path or `UartDrv` transmit API.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `feat: dispatch serial protocol lines`.

## Task 5: Create Local Attendance Polling Task

**Files:**
- Modify: `firmware/app/Src/attendance_app.c`
- Modify: `firmware/stm32/NFCAttend_Base/Core/Src/freertos.c`
- Modify: `docs/verification/firmware-bringup.md`

- [ ] Add anti-repeat tracking using UID and configured repeat interval.
- [ ] Create a low-frequency NFC polling task that calls `attendance_app_poll_nfc()`.
- [ ] Print append success, duplicate skip, no-card silence and storage errors over USART1.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `feat: poll nfc attendance records`.

## Task 6: Link Existing Network Module

**Files:**
- Modify: `firmware/stm32/NFCAttend_Base/Makefile`
- Modify: `docs/tasks/firmware-framework-status.md`

- [ ] Add `../Bsp/ESP01/esp01s.c` and `../../app/Src/att_network.c` to `C_SOURCES`.
- [ ] Add `-I../Bsp/ESP01` to `AS_INCLUDES` and `C_INCLUDES`.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `feat: link firmware network modules`.

## Task 7: Add Network Polling Schedule

**Files:**
- Modify: `firmware/stm32/NFCAttend_Base/Core/Src/freertos.c`
- Modify: `firmware/app/Src/att_network.c`
- Modify: `server/server.py`

- [ ] Add periodic calls for heartbeat and pending upload without blocking local attendance.
- [ ] Mark records uploaded only after an ACK parsing path is available; until then, leave records pending after send attempts.
- [ ] Ensure the Python server ACK format stays compatible with the firmware upload sequence.
- [ ] Run `python -m compileall pc_tool server`.
- [ ] Run `python pc_tool/tests/test_core.py`.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `feat: schedule network upload polling`.

## Task 8: Final Documentation And Verification

**Files:**
- Modify: `docs/project-status.md`
- Modify: `docs/tasks/firmware-framework-status.md`
- Modify: `docs/work-log/2026-06-29.md`
- Modify: `docs/verification/firmware-bringup.md`

- [ ] Record all host verification commands and results.
- [ ] Keep hardware validation items marked pending unless real board evidence was produced in this session.
- [ ] Run `python -m compileall pc_tool server`.
- [ ] Run `python pc_tool/tests/test_core.py`.
- [ ] Run `make clean; make` in `firmware/stm32/NFCAttend_Base`.
- [ ] Commit with `docs: update continuation status`.
- [ ] Push `main` and `feature/continue-requirements` to `origin`.
