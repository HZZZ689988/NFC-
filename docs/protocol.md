# Protocol

## CRC16

All new firmware protocol frames use `CRC16-CCITT-FALSE`:

```text
poly    = 0x1021
init    = 0xFFFF
refin   = false
refout  = false
xorout  = 0x0000
check   = 0x29B1 for "123456789"
```

Frame format:

```text
$PAYLOAD*FFFF\n
```

Legacy newline commands are still accepted during bring-up.

## Serial Commands

```text
READ
DIAG?
ISSUE:UID,SID,POINTS,CARD_TYPE
IMGA00:HEX32
IMGN00:HEX32
IMGD00:HEX32
UPDATEIMG
CLEAR:UID
LIST:<count>
LIST:ALL
SIMATT:A1B2C3D4,1001,2
UITEST:OK
WEATHER?
WEATHERTEST:Sunny 20C
WEATHER!
TIME?
CFG?
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|TZ=8
CFG:SSID=wifi-name
CFG:PWD=wifi-password
CFG:HOST=192.168.1.10|PORT=9000
CFG:WKEY=weather-key|WLOC=hangzhou
PING
```

Expected replies:

```text
UID:A1B2C3D4
DIAG:RC522_RAW=0x92|RC522_VER=0x92|CMD=0x00|IRQ=0x01|FIFO=0x00|TX=0x03|ERR=0x00|PINS=0x3D|SHARE=1|REQ=-2|TAG=0400
OK
OK:ISSUE
OK:IMG
OK:UPDATEIMG
OK:CLEAR
OK:SIMATT:SEQ=13
OK:UITEST
WEATHER:Sunny 20C
OK:WEATHERTEST
TIME:1783011177|VALID=1
ERR:NO_CARD
ERR:UID_MISMATCH
ERR:CRC
ERR:NOT_READY
LIST:COUNT=12
REC:SEQ=12|UID=A1B2C3D4|SID=1001|NORMAL|1782691200|DEV=1|OK|UP=DONE
LIST:END
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.1.10|PORT=9000|TZ=8|SSID=wifi-name|WLOC=hangzhou
OK:CFG
```

`DIAG?` reads RC522 diagnostic registers without modifying card data. A healthy
MFRC522 clone normally reports `RC522_VER=0x91` or `0x92`, and `TX` should have
the lower antenna bits set after initialization. `RC522_RAW` is read before ISO
configuration, while `RC522_VER` is read after it. `PINS` is a GPIO snapshot:
bit0 `NSS`, bit1 `SCK`, bit2 `MOSI`, bit3 `MISO`, bit4 `RST`, bit5 W25Q128
`CS`. `SHARE=1` means the selected RC522 MOSI pin is also the W25Q128 chip
select pin on this board. `RC522_VER=0x00` or `0xFF` means the MCU is not
communicating with the RC522 over the configured wiring.

`LIST:<count>` returns the newest `<count>` records, for example `LIST:1`.
`LIST:ALL` returns every stored record in storage order. Bad list counts, including
the literal placeholder `LIST:N`, return `ERR:ARG` without sending a partial list.
`UP=` reports upload state: `PENDING`, `DONE` or `FAILED`.

`SIMATT:<uid>,<sid>,<type>` is a board-verification command used while RC522 is
paused. It appends a pending attendance record through the application layer,
updates the display/feedback path and schedules immediate network upload. The
record type is `0` for `IN`, `1` for `OUT` and `2` for `NORMAL`.

`UITEST:<case>` is a board-verification command for OLED and local feedback
while RC522 is paused. Supported cases are `READY`, `OK`, `DUP`, `INVALID`,
`ERROR`, `NETOK` and `NETERR`.

`WEATHER?` reads the cached LittleFS `/weather.txt` value and returns
`WEATHER:<text>`. Empty or missing cache is reported as `WEATHER:WEATHER --`.
`WEATHERTEST:<text>` stores a short test weather string, updates the OLED weather
page and returns `OK:WEATHERTEST`. `WEATHER!` forces one ESP01S weather query
when WiFi/network is ready and weather config is present; it returns
`WEATHER:<text>` on success or `ERR:NOT_READY` / `ERR:WEATHER` on failure. The
upper-computer device-config tab exposes all three weather diagnostic commands.

`TIME?` returns the application's current time source as `TIME:<unix>|VALID=<0|1>`.
`VALID=1` means the value is at or after 2021-01-01 and should be treated as
RTC/NTP-derived Unix time. `VALID=0` means the firmware is still using the small
RTOS uptime fallback. The upper-computer device-config tab exposes this query.

## Device Config

`CFG?` returns the current persistent device/network config summary. `CFG:` updates one or more fields and stores the result in LittleFS `/config.bin`; firmware then reapplies the runtime display and network model.

Supported fields:

- `DEV`: device id, `1..4294967295`.
- `MODE`: work mode, `0=normal`, `1=check-in`, `2=check-out`, `3=in/out`.
- `UPLOAD`: `0` or `1`.
- `REPEAT`: anti-repeat interval in seconds, `0..65535`.
- `SSID`, `PWD`: WiFi SSID/password.
- `HOST`, `PORT`: TCP server host and port.
- `WKEY`, `WLOC`: weather API key and location.
- `TZ`: timezone offset, `-12..14`.

String values must not contain `|`, `=` or control characters. The upper-computer sends long config as multiple `CFG:` lines so each line fits the firmware serial buffer.

## Image Card Blocks

Image card block commands write one 16-byte Mifare data block per command:

```text
IMGA00..IMGA23: portrait bitmap blocks
IMGN00..IMGN09: name bitmap blocks
IMGD00..IMGD09: department bitmap blocks
UPDATEIMG: finish the image update session
```

The payload after `:` is exactly 32 uppercase or lowercase hex characters. Firmware writes only Mifare data blocks and skips every sector trailer block.

Current block layout:

- Account block: sector 0 block 1.
- Portrait: sectors 1-8, blocks 0-2 in each sector, 24 blocks total.
- Name: starts at sector 9 block 0 and uses the next 10 data blocks.
- Department: starts after the name blocks and uses the next 10 data blocks.

`UPDATEIMG` succeeds only after all 24 portrait blocks, 10 name blocks and 10 department blocks have been received for the same image card UID. Otherwise it returns `ERR:NOT_READY`.

## UID Forced Consistency

When `ISSUE` or `CLEAR` is received, the MCU must:

1. Scan the current RC522 card.
2. Compare current UID with the UID from the command.
3. Return `ERR:NO_CARD` if no card is present.
4. Return `ERR:UID_MISMATCH` if UID differs.
5. Only write or clear the card after the UID matches.

## Host Protocol Test

Firmware host-side mock tests cover protocol routing, USART line buffering,
local NFC polling and network polling/parsing. The runner selects `gcc`,
`clang`, `zig` or MSVC `cl`, then builds temporary executables outside the
repository:

```powershell
python firmware/app/tests/run_host_tests.py
```

## Network Upload

ESP01S transparent TCP line format:

```text
HEARTBEAT:DEV=1
UPLOAD:SEQ=1|UID=A1B2C3D4|SID=1001|TYPE=2|TS=1782691200|DEV=1
```

Server replies:

```text
ACK:HEARTBEAT
ACK:UPLOAD:1
ERR:UNKNOWN
```
