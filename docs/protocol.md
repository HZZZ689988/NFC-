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
ISSUE:UID,SID,POINTS,CARD_TYPE
IMGA00:HEX32
IMGN00:HEX32
IMGD00:HEX32
UPDATEIMG
CLEAR:UID
LIST:N
LIST:ALL
CFG?
PING
```

Expected replies:

```text
UID:A1B2C3D4
OK
OK:ISSUE
OK:IMG
OK:UPDATEIMG
OK:CLEAR
ERR:NO_CARD
ERR:UID_MISMATCH
ERR:CRC
ERR:NOT_READY
LIST:COUNT=12
REC:SEQ=12|UID=A1B2C3D4|SID=1001|NORMAL|1782691200|DEV=1|OK
LIST:END
```

`LIST:N` returns the newest `N` records. `LIST:ALL` returns every stored record in storage order. Bad list counts return `ERR:ARG` without sending a partial list.

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

The firmware protocol handler has a host-side mock test for command routing:

```powershell
gcc -I firmware/app/Inc firmware/app/tests/test_att_protocol_host.c firmware/app/Src/att_protocol.c firmware/app/Src/att_crc16.c -o firmware/app/tests/test_att_protocol_host.exe
firmware\app\tests\test_att_protocol_host.exe
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
