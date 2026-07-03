---
type: design
project: nfc-attendance
updated: 2026-07-03
---

# Bootloader Design

## Flash Map

STM32F407VETx internal Flash is 512 KiB:

```text
0x08000000..0x08003FFF  sector 0  16K
0x08004000..0x08007FFF  sector 1  16K
0x08008000..0x0800BFFF  sector 2  16K
0x0800C000..0x0800FFFF  sector 3  16K
0x08010000..0x0801FFFF  sector 4  64K
0x08020000..0x0803FFFF  sector 5  128K
0x08040000..0x0805FFFF  sector 6  128K
0x08060000..0x0807FFFF  sector 7  128K
```

A/B partitioning:

```text
0x08000000..0x0800FFFF  bootloader, 64K
0x08010000..0x0803FFFF  slot A, 192K, sectors 4-5
0x08040000..0x0807FFFF  slot B, 256K, sectors 6-7
```

The normal app still builds at `0x08000000` by default for direct DAP flashing.
Bootloader-managed builds are explicit:

```powershell
cd firmware\stm32\NFCAttend_Base
make APP_SLOT=A -j4
make APP_SLOT=B -j4
```

`APP_SLOT=A` links at `0x08010000` and sets `VECT_TAB_OFFSET=0x10000`.
`APP_SLOT=B` links at `0x08040000` and sets `VECT_TAB_OFFSET=0x40000`.
The legacy `make APP_OFFSET=1` path remains compatible with slot A.

## W25Q128 Files

The bootloader mounts the same W25Q128 LittleFS volume used by the app:

```text
/ota.bin
/ota.meta
/bootstate.bin
```

`ota.meta` uses `att_ota_meta_t` from `firmware/app/Inc/att_ota_format.h`.
Important fields:

- `target_slot = 1` for slot A or `2` for slot B.
- `target_addr` must match the target slot base address.
- `size_bytes` must fit the target slot.
- `expected_crc32` must match the cached image and programmed Flash.
- `verified = 1`.
- `install_state = PENDING` or `INSTALLING`.

`bootstate.bin` uses `att_boot_state_t` and stores:

- `active_slot`: slot selected by the bootloader.
- `pending_slot`: newly installed slot waiting for app confirmation.
- `confirmed_slot`: last slot confirmed by a successfully started app.
- `boot_count`: pending-slot boot attempts.

## Boot Flow

1. Initialize HAL, GPIO, SPI1 and W25Q128.
2. Mount W25Q128 LittleFS.
3. If `/ota.meta` is installable, validate `/ota.bin`.
4. Erase only the target slot sectors.
5. Program `/ota.bin` into the target slot.
6. Verify internal Flash CRC32 and app vector table.
7. Mark `bootstate.pending_slot` and `bootstate.active_slot` to the target slot.
8. Mark OTA metadata `INSTALLED`, or `FAILED` with an error code.
9. Select a boot slot:
   - Prefer a valid pending slot.
   - If the app confirms the pending slot, clear `pending_slot`.
   - If pending boot attempts reach 3, fall back to `confirmed_slot`.
   - If metadata is missing, boot any valid confirmed/active slot.
10. Jump to the selected slot, or blink in the bootloader error loop.

The app confirms the running slot during `attendance_app_init()` through
`att_storage_boot_confirm_current()`. That writes `confirmed_slot` and clears
`pending_slot` when the app was booted from a pending slot.

## OTA Server Files

The firmware requests the opposite slot:

```text
OTA?SLOT=A
OTA?SLOT=B
OTA:GET:OFFSET=0|LEN=192|SLOT=A
OTA:GET:OFFSET=0|LEN=192|SLOT=B
```

The server serves slot-specific files when requested:

```text
server/data/ota/current_A.bin
server/data/ota/version_A.txt
server/data/ota/current_B.bin
server/data/ota/version_B.txt
```

Legacy `current.bin` and `version.txt` remain supported for old clients or
manual bring-up. A slot-specific response includes `SLOT=A` or `SLOT=B`; the
firmware rejects a response that names the wrong slot.

## Build And Flash

Build all bootloader-managed artifacts:

```powershell
cd firmware\stm32\NFCAttend_Base
make APP_SLOT=A -j4
make APP_SLOT=B -j4

cd ..\NFCAttend_Bootloader
make -j4
```

Initial DAP programming sequence:

```powershell
cd firmware\stm32\NFCAttend_Bootloader
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/NFCAttend_Bootloader.elf verify reset exit"

cd ..\NFCAttend_Base
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build_app_a/Demo_W25Q128_AppA.elf verify reset exit"
```

After that, prepare an OTA package for the opposite slot:

```powershell
Copy-Item build_app_b\Demo_W25Q128_AppB.bin ..\..\..\server\data\ota\current_B.bin
Set-Content ..\..\..\server\data\ota\version_B.txt "app-b-20260703"
```

The board running slot A should request `OTA?SLOT=B`, download `current_B.bin`,
accept `OTARST`, install slot B, boot slot B and later report `CUR=B|SLOT=A`
from USART `OTA?`.

To test B to A, copy `build_app_a\Demo_W25Q128_AppA.bin` to `current_A.bin`
and set `version_A.txt`.

## Current Limits

- No signed firmware authentication yet.
- No serial recovery shell inside the bootloader.
- Slot A is 192 KiB. Current firmware fits, but future growth must be checked
  against the smaller slot, not only slot B.
- Rollback depends on app startup confirmation. A failure after early app init
  can still be marked confirmed.
