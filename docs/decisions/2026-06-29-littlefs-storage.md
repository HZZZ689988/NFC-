---
type: decision
date: 2026-06-29
status: accepted
---

# Use LittleFS On W25Q128

## Context

The project needs persistent config, attendance records, upload retry state and optional weather cache. The user explicitly chose LittleFS instead of a raw Flash circular log.

## Decision

Use LittleFS on the W25Q128 external SPI Flash.

## Consequences

- Easier file-style storage for config, records and cache data.
- Better built-in wear leveling and power-loss behavior than ad hoc raw storage.
- More integration work: block device port, RAM buffers, mount/format flow and hardware validation.
- Fixed-size records are still stored in an append-oriented binary file so `LIST:N` remains straightforward.
