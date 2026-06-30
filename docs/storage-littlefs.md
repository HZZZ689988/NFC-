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
/weather.txt      Cached weather display data
```

Future files:

```text
/net_state.bin    Upload retry metadata if records.bin rewrite cost becomes high
```

## Important Notes

- LittleFS `prog` must write only to erased flash. The port uses `W25QXX_Write_NoCheck`, not `W25QXX_Write`.
- `erase` maps one LittleFS block to one W25Q128 4 KB sector.
- If another subsystem needs raw Flash, reserve a block range and reduce LittleFS `block_count` or add an address offset.
- Hardware validation is required before treating this as done.
