# NFC 考勤系统上位机

Python/Tkinter 上位机工具，用于串口连接 STM32 下位机、读卡、发卡、发送图像块、导入考勤记录和导出 CSV。

## 功能

- 串口连接、端口刷新、收发日志。
- `READ` 读卡、`CLEAR` 清卡。
- 人员信息录入：UID、工号、姓名、部门、积分、卡类型。
- 头像缩放为 `48x64` 单色位图。
- 姓名、部门渲染为 `80x16` 单色位图。
- 发卡命令：
  - `ISSUE:UID,SID,POINTS,CARD_TYPE`
  - `IMGA00..23`
  - `IMGN00..09`
  - `IMGD00..09`
  - `UPDATEIMG`
- SQLite 本地保存人员、发卡日志和考勤记录。
- 支持 `LIST:N` 从下位机读取记录。
- 设备配置页支持 `WEATHER?`、`WEATHERTEST:<text>` 和 `WEATHER!` 天气缓存诊断。
- 新协议支持 `$PAYLOAD*CRC16`，旧 ASCII 命令暂时保留兼容。

## 运行

```powershell
cd pc_tool
python -m pip install -r requirements.txt
python run.py
```

## 测试

```powershell
cd pc_tool
python tests/test_core.py
```

或者：

```powershell
python -m compileall .
```

## 数据

运行后会自动创建：

```text
pc_tool/data/attendance.db
```

`data/` 不提交到仓库。
