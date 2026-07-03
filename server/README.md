# NFC Attendance Server

One Python process provides both the ESP01S TCP receiver and a small Web backend.

Run:

```powershell
cd server
python server.py --host 0.0.0.0 --port 9000 --web-port 8080
```

Open the Web backend at:

```text
http://127.0.0.1:8080/
```

The TCP receiver accepts legacy newline frames and CRC-wrapped frames:

```text
HEARTBEAT:DEV=1|TEMP=26.5|FW=1.2.3
UPLOAD:SEQ=1|UID=A1B2C3D4|SID=1001|TYPE=2|TS=1782691200|DEV=1
UPLOADB:DEV=1|R=2,A1B2C3D4,1001,0,1782691201;3,11223344,1002,1,1782691202
BL?
OTA?
OTA?SLOT=B
OTA:GET:OFFSET=0|LEN=192
OTA:GET:OFFSET=0|LEN=192|SLOT=B
CRC:UPLOAD:SEQ=4|UID=AABBCCDD|SID=1003|TYPE=2|TS=1782691203|DEV=1*FFFF
```

Responses:

```text
ACK:HEARTBEAT|BL=<active_blacklist_count>
ACK:UPLOAD:<seq>
ACK:UPLOADB:<seq>,<seq>
BL:COUNT=<count>|UIDS=<uid>,<uid>
OTA:NONE
OTA:VERSION=<version>|SIZE=<bytes>|CRC32=<crc32>|CHUNK=192
OTA:VERSION=<version>|SIZE=<bytes>|CRC32=<crc32>|CHUNK=192|SLOT=B
OTA:DATA:OFFSET=<offset>|LEN=<len>|CRC32=<chunk_crc32>|HEX=<bytes_hex>
OTA:END|SIZE=<bytes>|CRC32=<crc32>
ERR:CRC
ERR:UNKNOWN
```

SQLite data is stored in `server/data/attendance.db`:

- `uploads`: attendance records, deduplicated by `(dev, seq)`.
- `devices`: latest heartbeat/upload state, online status source, temperature and firmware.
- `blacklist`: active or disabled local-reject card UIDs.
- `ota_packages`: reserved metadata table; OTA currently uses
  `server/data/ota/current.bin` and `server/data/ota/version.txt` for legacy
  packages, or slot-specific `current_A.bin`/`version_A.txt` and
  `current_B.bin`/`version_B.txt` for A/B packages.

The Web backend provides:

- `/devices`: device list, heartbeat/upload counts, online/offline state and temperature.
- `/records`: attendance records with device, SID and time filters.
- `/blacklist`: add and toggle blacklist UIDs. Firmware refreshes this list with `BL?` after heartbeat and rejects matching cards locally.
- `/ota`: current OTA package metadata. Firmware downloads the package into
  W25Q128, verifies CRC32, and the bootloader installs it into the target
  internal Flash slot. With A/B bootloader firmware, use
  `firmware/stm32/NFCAttend_Base/build_app_a/Demo_W25Q128_AppA.bin` as
  `server/data/ota/current_A.bin` and
  `firmware/stm32/NFCAttend_Base/build_app_b/Demo_W25Q128_AppB.bin` as
  `server/data/ota/current_B.bin`.

Each received TCP line is also appended to `server/data/uploads.log` for simple demo evidence.
