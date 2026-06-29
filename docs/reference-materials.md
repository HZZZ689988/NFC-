# Reference Materials

## Source Location

Original working directory:

```text
D:\c_work
```

Extracted archive:

```text
D:\c_work\extracted\C_work
```

Repository copy:

```text
D:\c_work\nfc_attendance_project
```

## Submitted Into Repository

| Material | Repository path | Purpose |
| --- | --- | --- |
| STM32 W25Q128 demo base | `firmware/stm32/NFCAttend_Base` | Buildable HAL/FreeRTOS firmware base |
| BSP drivers | `firmware/stm32/Bsp` | RC522, W25Q128, ESP01S, OLED, UART, Key, LED and other drivers |
| LittleFS source | `firmware/third_party/littlefs` | Required filesystem on W25Q128 |
| Upper-computer tool | `pc_tool` | Python/Tkinter serial card issuing and attendance management |
| Test TCP server | `server` | Stage-3 upload and heartbeat test receiver |
| Protocol and design docs | `docs` | Requirements, protocol, storage, hardware and bring-up notes |

## Local Reference Materials Not Submitted

These files are kept out of the repository to avoid large binary/source-archive churn:

| Local file or folder | Reason not committed | How it is used |
| --- | --- | --- |
| `D:\c_work\C_work.zip` | Large original archive | Source material for extracted BSP and demos |
| `D:\c_work\2026专业实践综合设计II——NFC考勤系统设计.pdf` | PDF assignment/reference material | Requirements and course context |
| `D:\c_work\NFC 考勤系统设计指导.pdf` | PDF guide | Development guidance |
| `D:\c_work\extracted\C_work\发卡读卡演示程序` | Contains EXE/HEX demo binaries | Manual comparison and reference only |

## Important Extracted Paths

| Extracted path | Notes |
| --- | --- |
| `extracted/C_work/NFCAttend/NFCAttend.ioc` | CubeMX configuration for the target hardware |
| `extracted/C_work/BSP/Demo_W25Q128` | Selected firmware base because it already has W25Q128 and FreeRTOS |
| `extracted/C_work/BSP/Demo_ESP01S` | ESP01S reference demo |
| `extracted/C_work/BSP/Demo_RC522` | RC522 reference demo |
| `extracted/C_work/BSP/Demo_OLED` | OLED reference demo |
| `extracted/C_work/BSP/Bsp/w25qxx` | W25Q128 driver |
| `extracted/C_work/BSP/Bsp/NFC` | RC522 driver |
| `extracted/C_work/BSP/Bsp/ESP01` | ESP01S driver |
| `extracted/C_work/BSP/Bsp/OLED` | OLED driver |
| `extracted/C_work/BSP/Bsp/UartDrv` | UART abstraction driver |

## Traceability

| Requirement area | Primary reference |
| --- | --- |
| Hardware pin map | `docs/hardware-map.md`, `NFCAttend.ioc` |
| Storage design | `docs/storage-littlefs.md`, `firmware/app/Src/att_lfs_port.c` |
| CRC16 protocol | `docs/protocol.md`, `firmware/app/Src/att_crc16.c`, `pc_tool/nfc_attendance_tool/protocol.py` |
| UID forced consistency | `docs/protocol.md`, `firmware/app/Src/att_card.c` |
| Stage-3 networking | `docs/development-plan.md`, `firmware/app/Src/att_network.c`, `server/server.py` |
| Bring-up state | `docs/project-status.md`, `docs/verification/firmware-bringup.md` |
