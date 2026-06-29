# Requirements

## 1. Project Goal

Build an NFC attendance system using STM32F407VET6 as the lower-computer controller and a Python upper-computer tool for card issuing and record management.

The system must support offline attendance, persistent storage, card issuing, local display and required stage-3 networking.

## 2. Hardware Scope

Required hardware modules:

- STM32F407VET6 main controller.
- RC522 NFC reader/writer.
- W25Q128 external SPI Flash.
- OLED display over I2C.
- ESP01S WiFi module.
- USART1 connection to PC upper-computer.
- Keys, LEDs and buzzer for local interaction.
- RTC with LSE clock source.

Detailed pin mapping is maintained in `docs/hardware-map.md`.

## 3. Upper-Computer Requirements

The upper-computer must:

- Connect to the device over serial port.
- Read card UID.
- Issue normal, image and admin cards.
- Enforce that issued data contains the UID read from the physical card.
- Send account data and image blocks.
- Clear a card after UID confirmation.
- Import attendance records from the device.
- Store people, issue logs and attendance records in SQLite.
- Export attendance records to CSV.
- Support CRC16 framed protocol while keeping legacy ASCII commands during bring-up.

## 4. Firmware Requirements

The firmware must:

- Initialize HAL, FreeRTOS and all board peripherals.
- Mount or format LittleFS on W25Q128.
- Create default device config on first boot or invalid config CRC.
- Read and write Mifare card account blocks through RC522.
- Reject `ISSUE` and `CLEAR` when no card is present.
- Reject `ISSUE` and `CLEAR` when current card UID differs from command UID.
- Record attendance locally when a valid card is presented.
- Prevent repeated attendance within the configured anti-repeat interval.
- Store attendance records persistently.
- Serve serial commands from the PC tool.
- Display device state, attendance result, network state and weather on OLED.

## 5. Networking Requirements

Stage 3 is mandatory.

The device must:

- Load WiFi and server config from LittleFS.
- Initialize ESP01S.
- Connect to WiFi.
- Sync RTC time using NTP.
- Query weather data.
- Send heartbeat packets to the server.
- Upload pending attendance records.
- Keep records pending while offline.
- Retry upload after network recovery.

## 6. Storage Requirements

Storage must use W25Q128 with LittleFS.

Required files:

- `/config.bin`: device config with magic, version and CRC16.
- `/records.bin`: fixed-size attendance records with CRC16.

Future files may include:

- `/weather.txt`
- `/net_state.bin`

The LittleFS block-device port must use 4 KB erase blocks and must not call a write function that erases sectors inside LittleFS `prog`.

## 7. Protocol Requirements

New protocol frames must use:

```text
CRC16-CCITT-FALSE
poly=0x1021
init=0xFFFF
refin=false
refout=false
xorout=0x0000
```

Frame format:

```text
$PAYLOAD*CRC16\n
```

Legacy commands may remain available until hardware bring-up is stable.

## 8. Minimum Demo Requirements

A complete demo must show:

- Upper-computer opens the serial port.
- Upper-computer reads a card UID.
- Upper-computer issues the card.
- Firmware rejects issuing if the card is changed before writing.
- Device records one attendance event offline.
- Device survives reset without losing the record.
- Upper-computer imports the record.
- ESP01S uploads the record to the test server.
- Server logs heartbeat and upload packets.

## 9. Validation Requirements

Do not mark firmware done until real hardware validation is recorded.

Required evidence:

- Build output.
- W25Q128 ID and LittleFS mount result.
- RC522 UID read result.
- UID mismatch rejection result.
- Record append/read result.
- ESP01S WiFi, NTP and TCP upload result.
- OLED display photos or logs if available.
