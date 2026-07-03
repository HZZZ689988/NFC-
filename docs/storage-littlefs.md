# LittleFS Storage Design

## Decision

Use W25Q128 + LittleFS for the project storage layer.

## Layout

Current framework uses the whole W25Q128 as one LittleFS volume:

```text
block_size   = 4096
block_count  = 4096
prog_size    = 256
read_size    = 16
cache_size   = 256
lookahead    = 64
```

Files:

```text
/config.bin       Device config, versioned and CRC16 protected
/records.bin      Fixed-size attendance records, append first
/records.meta     Ring-buffer head/count metadata for records.bin
/weather.txt      Cached weather display data
/ota.bin          Cached bootloader-installable application image
/ota.meta         OTA image size, CRC32, target address and install state
/bootstate.bin    A/B active, pending, confirmed slot and boot-count state
```

Future files:

```text
/net_state.bin    Upload retry metadata if records.bin rewrite cost becomes high
```

## Important Notes

- LittleFS `prog` must write only to erased flash. The port uses `W25QXX_Write_NoCheck`, not `W25QXX_Write`.
- `erase` maps one LittleFS block to one W25Q128 4 KB sector.
- The bootloader mounts the same LittleFS volume and reads `/ota.bin`, `/ota.meta` and `/bootstate.bin`; it does not use a raw W25Q128 partition.
- Internal Flash A/B slots live at `0x08010000` and `0x08040000`; W25Q128 is only the OTA cache and metadata store, not a third executable slot.
- If another subsystem needs raw Flash, reserve a block range and reduce LittleFS `block_count` or add an address offset.
- Hardware validation is required before treating this as done.
