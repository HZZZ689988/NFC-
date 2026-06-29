# Continue Requirements Design

## Scope

Continue the NFC attendance project in small, reviewable commits that follow the existing `docs/development-plan.md` order. The next work focuses on host-buildable firmware integration first, then stage-3 networking support, then PC/server compatibility only where needed for firmware validation.

This design does not claim hardware completion. Real-board evidence still belongs under `docs/verification/` after flashing and testing W25Q128, RC522, ESP01S, OLED and RTC.

## Recommended Approach

Use the existing firmware application modules instead of rewriting the BSP layer:

- `attendance_app` remains the top-level application facade.
- `att_storage` owns LittleFS config and records.
- `att_card` owns RC522 UID/account operations.
- `att_protocol` owns serial line parsing and responses.
- `att_network` owns ESP01S heartbeat/upload/weather/time operations.
- `Core/Src/freertos.c` creates small polling tasks that call those facades.

This is lower risk than a large FreeRTOS rewrite because the current storage bootstrap already builds and runs from the demo task. It also keeps commits separable: build compatibility, app linkage, serial command behavior, attendance polling, network upload, then documentation.

## Data Flow

PC tool commands travel over USART1 as legacy ASCII or `$PAYLOAD*CRC16` frames. Firmware parses one line at a time, calls protocol handlers, and sends line responses. `READ`, `ISSUE`, `CLEAR`, `LIST` and `CFG?` must operate through `att_card` and `att_storage` so UID mismatch and no-card behavior are enforced at the firmware layer.

NFC polling reads RC522 UID through `att_card`. When a valid card account is available, the app creates an `att_record_t`, applies anti-repeat timing, appends it to LittleFS, and leaves it pending for upload.

Network polling reads the first pending record from LittleFS, sends it to the ESP01S TCP connection, and marks it uploaded only after the server ACK path is available. Heartbeat, NTP and weather remain separate periodic actions so failure in one path does not block local attendance.

## Error Handling

- CRC frame mismatch returns `ERR:CRC`.
- Invalid command arguments return `ERR:ARG`.
- Missing card returns `ERR:NO_CARD`.
- UID mismatch returns `ERR:UID_MISMATCH`.
- Storage failures return command-specific storage errors and do not modify records.
- Network unavailable leaves records pending.

## Verification

Each code slice must run the strongest available host verification before commit:

- `python -m compileall pc_tool server`
- `python pc_tool/tests/test_core.py`
- `make clean; make` in `firmware/stm32/NFCAttend_Base`

Hardware-only requirements remain documented as not validated until real evidence is recorded.
