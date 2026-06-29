---
type: project-status
project: nfc-attendance
updated: 2026-06-29
status: in-progress
---

# Project Status

## Current Focus

整体项目框架已经建立，包含下位机固件底座、应用层框架、LittleFS、上位机、阶段三测试服务端和文档。

## Implemented

- `pc_tool/`: Python/Tkinter 上位机，包含串口读写、发卡、图像块、SQLite、考勤导入。
- `firmware/stm32/NFCAttend_Base/`: 从 `Demo_W25Q128` 整理出的 STM32 HAL/FreeRTOS 底座。
- `firmware/stm32/Bsp/`: 已纳入 W25Q128、RC522、OLED、ESP01S、UART、Key、LED 等 BSP。
- `firmware/third_party/littlefs/`: 已纳入 LittleFS 源码。
- `firmware/app/`: 已建立 CRC16、LittleFS 存储、协议、发卡 UID 检查、网络、应用入口模块。
- `server/`: 已建立阶段三 TCP 上传测试服务端。

## Verified On Host

- `python -m compileall pc_tool server` passed.
- `python pc_tool/tests/test_core.py` passed.
- `make` passed in `firmware/stm32/NFCAttend_Base`.
- `firmware/app/Src/*.c` passed ARM GCC compile checks as standalone objects.

## Not Yet Hardware Validated

- LittleFS 在 W25Q128 上 mount/format/read/write。
- RC522 实卡 UID 强制一致发卡。
- ESP01S WiFi、NTP、天气和 TCP 上传。
- OLED 页面展示。

## Main Risks

- 现有 BSP 文档和部分注释存在编码损坏，但 C 接口可用。
- `firmware/app` 还没有接入 `Core/Src/freertos.c` 的实际任务调度。
- LittleFS 使用整片 W25Q128，若后续有其他裸 Flash 数据区，需要划分偏移区间。
