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
CARDLOCK:ON
CARDLOCK:OFF
CARDLOCK?
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
NET?
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
OK:CARDLOCK:ON
OK:CARDLOCK:OFF
CARDLOCK:ON
CARDLOCK:OFF
OK:ISSUE
OK:IMG
OK:UPDATEIMG
OK:CLEAR
OK:SIMATT:SEQ=13
OK:UITEST
WEATHER:Sunny 20C
OK:WEATHERTEST
TIME:1783011177|VALID=1
NET:READY=1|CFG=1|UPLOAD=1|STATE=6|TEXT=TRANSPARENT|NTP=1|BL=3|DEV=1|HOST=192.168.1.10|PORT=9000|SSID=wifi-name
ERR:NO_CARD
ERR:UID_MISMATCH
ERR:CRC
ERR:NOT_READY
LIST:COUNT=12
REC:SEQ=12|UID=A1B2C3D4|SID=1001|NORMAL|1782691200|DEV=1|OK|UP=DONE
LIST:END
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.1.10|PORT=9000|TZ=8|SSID=wifi-name|WLOC=hangzhou
OK:CFG
ADMIN:ON
ADMIN:SAVED
ADMIN:EXIT
ADMIN:TIMEOUT
ADMIN:ERR:CARD_DENIED
ADMIN:ERR:SAVE
ATTEND:OK:SEQ=1|TYPE=IN
ATTEND:OK:SEQ=2|TYPE=OUT|DUR=3600
ATTEND:OK:SEQ=3|TYPE=NORMAL
ATTEND:SKIP:DUPLICATE
ATTEND:ERR:ALREADY_IN
ATTEND:ERR:NO_ENTRY
ATTEND:ERR:INVALID_CARD
```

`DIAG?` reads RC522 diagnostic registers without modifying card data. A healthy
MFRC522 clone normally reports `RC522_VER=0x91` or `0x92`, and `TX` should have
the lower antenna bits set after initialization. `RC522_RAW` is read before ISO
configuration, while `RC522_VER` is read after it. `PINS` is a GPIO snapshot:
bit0 `NSS`, bit1 `SCK`, bit2 `MOSI`, bit3 `MISO`, bit4 `RST`, bit5 W25Q128
`CS`. `SHARE=1` means the selected RC522 MOSI pin is also the W25Q128 chip
select pin on this board. `RC522_VER=0x00` or `0xFF` means the MCU is not
communicating with the RC522 over the configured wiring.

`ISSUE:UID,SID,POINTS,CARD_TYPE` writes the account header to sector 0 block 1.
`CARD_TYPE` is `0` for normal cards, `1` for image cards and `2` for
administrator cards. Firmware rejects card types outside `0..2`.

`CARDLOCK:ON` pauses automatic attendance polling while the upper computer is
issuing, clearing, reading or transferring image-card blocks. `CARDLOCK:OFF`
resumes polling and clears the recent-UID anti-repeat state. `CARDLOCK?` returns
`CARDLOCK:ON` or `CARDLOCK:OFF`. The PC tool wraps every serial job with
`CARDLOCK:ON/OFF` so asynchronous `ATTEND:*` lines do not interrupt the command
response sequence.

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

`NET?` returns the runtime ESP01S link snapshot as
`NET:READY=<0|1>|CFG=<0|1>|UPLOAD=<0|1>|STATE=<n>|TEXT=<state>|NTP=<0|1>|BL=<count>|DEV=<id>|HOST=<host>|PORT=<port>|SSID=<ssid>`.
`READY=1` means the ESP01S is already in transparent TCP mode. `STATE/TEXT`
follow the ESP01S startup sequence: `IDLE`, `AT_OK`,
`WIFI_CONNECTING`, `WIFI_CONNECTED`, `TCP_CONNECTING`, `TCP_CONNECTED` and
`TRANSPARENT`. If `TEXT=WIFI_CONNECTING` persists, the module has not associated
with the configured WiFi yet.

## Local Attendance Events

When a valid account card is detected by the RC522 polling path, firmware emits
an asynchronous `ATTEND:*` line. In normal mode (`MODE=0`) the record type is
`NORMAL`. In check-in mode (`MODE=1`) a card with an unmatched latest `IN`
record is rejected as `ATTEND:ERR:ALREADY_IN`. In check-out mode (`MODE=2`) a
card without an unmatched latest `IN` record is rejected as
`ATTEND:ERR:NO_ENTRY`. In alternating mode (`MODE=3`) the same UID alternates
between `IN` and `OUT`.

OUT events include `DUR=<seconds>`, calculated from the latest matching `IN`
timestamp. Because the value is second-based, crossing midnight does not need a
special date branch as long as the device time source is monotonic.

## Administrator Mode

A valid account card with `CARD_TYPE=2` enters administrator mode and emits
`ADMIN:ON`. Administrator mode lasts 120 seconds after the latest administrator
operation. During administrator mode, normal account cards are rejected as
`ADMIN:ERR:CARD_DENIED` and no attendance record is written.

Physical key behavior in administrator mode:

- `K1`: exit without saving.
- `K2` / `K3`: switch between `DEV` and `MODE`.
- `K4` / `K5`: decrement or increment the selected value.
- `K6`: save `/config.bin`, emit `ADMIN:SAVED`, then reset the MCU.

Only `DEV` and `MODE` are editable on the device. `MODE` cycles through
`1=check-in`, `2=check-out` and `3=in/out`; `MODE=0` is still available through
the serial `CFG:` command.

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

ESP01S transparent TCP frames now use the same `CRC16-CCITT-FALSE` payload
wrapper as the serial protocol. The server still accepts legacy unwrapped lines
for bring-up and database replay tests.

Wrapped format:

```text
CRC:<payload>*FFFF
```

Payload examples:

```text
HEARTBEAT:DEV=1|TEMP=0
UPLOAD:SEQ=1|UID=A1B2C3D4|SID=1001|TYPE=2|TS=1782691200|DEV=1
UPLOADB:DEV=1|R=2,A1B2C3D4,1001,0,1782691201;3,11223344,1002,1,1782691202
BL?
OTA?
OTA?SLOT=B
OTA:GET:OFFSET=0|LEN=192
OTA:GET:OFFSET=0|LEN=192|SLOT=B
OTARST
```

Server replies:

```text
ACK:HEARTBEAT|BL=2
ACK:UPLOAD:1
ACK:UPLOADB:2,3
BL:COUNT=2|UIDS=A1B2C3D4,11223344
OTA:NONE
OTA:VERSION=fw-20260703-192|SIZE=116668|CRC32=25A687EF|CHUNK=192
OTA:VERSION=app-b-20260703|SIZE=117224|CRC32=11223344|CHUNK=192|SLOT=B
OTA:DATA:OFFSET=0|LEN=8|CRC32=C4ABFFF5|HEX=0102A55A10203040
OTA:END|SIZE=8|CRC32=C4ABFFF5
ERR:CRC
ERR:UNKNOWN
```

If a request was CRC-wrapped, `server/server.py` wraps the response as
`CRC:<response>*FFFF`; firmware verifies the CRC before applying upload ACKs or
blacklist updates. Invalid CRC responses are ignored. Prefixed ESP-style lines
such as `+IPD,...:ACK:UPLOAD:<seq>` are still accepted for legacy unwrapped
bring-up runs.

`UPLOADB` batches up to three pending records from firmware to reduce TCP line
overhead during backlog recovery. A batch ACK lists the accepted sequences, and
firmware marks each listed sequence uploaded. Single pending records still use
the legacy `UPLOAD:` payload for compatibility.

`BL?` returns up to 16 active blacklist UIDs. Firmware refreshes the local list
after heartbeat and rejects matching cards before duplicate checks or record
append, emitting `ATTEND:ERR:BLACKLIST` and local failure feedback.

`OTA?` returns `OTA:NONE` when no package is present. A/B OTA clients request the
opposite internal Flash slot with `OTA?SLOT=A` or `OTA?SLOT=B`; chunk requests
carry the same `SLOT` field. The server first looks for slot-specific files:

```text
server/data/ota/current_A.bin
server/data/ota/version_A.txt
server/data/ota/current_B.bin
server/data/ota/version_B.txt
```

Legacy `current.bin` and `version.txt` are still accepted for old clients and
manual bring-up. Slot-specific advertisements include `SLOT=A` or `SLOT=B`;
firmware rejects an advertisement that names the wrong target slot.

The server advertises version, byte size, whole-file CRC32 and maximum chunk
size, currently 192 bytes. Firmware then requests sequential `OTA:GET` chunks,
verifies each chunk CRC32, writes the bytes into W25Q128 LittleFS as `ota.bin`,
and verifies the complete cached image before reporting `OTA:STATE=READY` over
USART. A verified cache is marked in `ota.meta` as `INSTALL=PENDING` with a
target slot and base address:

- Slot A: `SLOT=A`, `TARGET=0x08010000`, max 192 KiB.
- Slot B: `SLOT=B`, `TARGET=0x08040000`, max 256 KiB.

The USART `OTA?` cache response includes `CUR=<A/B/->`, `SLOT=<A/B/->`,
`TARGET=<hex>`, `INSTALL=<state>` and `ERR=<code>`. Install states are:
`0=NONE`, `1=PENDING`, `2=INSTALLING`, `3=INSTALLED`, `4=FAILED`. The bootloader
uses `ota.bin`, `ota.meta` and `bootstate.bin` in W25Q128 LittleFS to install,
select and confirm slots.

The local USART command `OTARST` triggers a reboot into the bootloader only when
the cached image is verified, fully received, targeted at the current opposite
slot, CRC32 matches, and `INSTALL` is `PENDING` or `INSTALLING`. It replies
`OK:OTARST` before reset, or `ERR:OTA_NOT_READY` when the cache is not
installable.

`server/server.py` keeps the legacy text log and persists valid uploads to
`server/data/attendance.db`. Tables:

- `uploads`: attendance records deduplicated by `(DEV, SEQ)`.
- `devices`: peer, heartbeat count, upload count, latest temperature, firmware
  string and active blacklist count. Devices are considered online when
  `last_seen` is within 90 seconds.
- `blacklist`: active/disabled UIDs managed by the Web backend.
- `ota_packages`: reserved for later OTA metadata.

The Web backend is served by the same process:

```powershell
python server.py --host 0.0.0.0 --port 9000 --web-port 8080
```

- `/devices`: devices, online state, heartbeat/upload counts, temperature.
- `/records`: attendance records with `dev`, `sid`, `from` and `to` filters.
- `/blacklist`: add or toggle local-reject UIDs.
- `/ota`: current OTA package metadata for W25Q128 download/verify.

Network polling gives upload traffic priority over weather queries. If a
pending upload frame is sent in the current poll, the weather query is skipped
until a later idle poll so the ESP01S transparent TCP connection is not
interrupted during backlog upload.
