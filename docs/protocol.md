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
ERR:NOCARD
ERR:CID_MISMATCH
ERR:CRC
LIST:COUNT=12
LIST:END
```

## UID Forced Consistency

When `ISSUE` or `CLEAR` is received, the MCU must:

1. Scan the current RC522 card.
2. Compare current UID with the UID from the command.
3. Return `ERR:NOCARD` if no card is present.
4. Return `ERR:CID_MISMATCH` if UID differs.
5. Only write or clear the card after the UID matches.

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
