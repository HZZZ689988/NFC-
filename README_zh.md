# NFC 考勤系统使用说明

本项目是基于 STM32F407VET6 的 NFC 考勤系统，包含下位机固件、Python 上位机、TCP/Web 服务器后台和项目验证文档。系统支持 RC522 读写卡、W25Q128 离线存储、OLED 显示、按键管理员模式、ESP01S 联网、服务器黑名单、批量上传、天气、实时温度和 A/B 分区 OTA。

## 1. 项目结构

```text
firmware/
  app/                         考勤业务逻辑、协议、存储、联网、显示、OTA
  stm32/NFCAttend_Base/        STM32 HAL/FreeRTOS 工程
  stm32/NFCAttend_Bootloader/  A/B OTA bootloader 工程
  stm32/Bsp/                   RC522、W25Q128、OLED、ESP01S、按键、LED、蜂鸣器驱动
  third_party/littlefs/        LittleFS 文件系统
pc_tool/                       Python/Tkinter 上位机，可读卡、发卡、导入记录、打包 exe
server/                        TCP 接收服务和 Web 后台
docs/                          需求、协议、硬件、存储、启动和验证记录
tools/                         验证脚本
```

## 2. 硬件要求

- STM32F407VET6 主控板
- RC522 NFC 读写模块
- W25Q128 SPI Flash
- OLED 显示屏
- ESP01S WiFi 模块
- USB 串口，默认 `COM3`，`115200`
- 按键 K1-K6、LED、蜂鸣器
- CMSIS-DAP 或兼容下载器

详细管脚见 [docs/hardware-map.md](docs/hardware-map.md)。

## 3. 安装依赖

PC 侧需要：

- Python 3.10 或更高
- `arm-none-eabi-gcc`
- `make`
- OpenOCD 或 STM32 下载工具

安装 Python 依赖：

```powershell
cd pc_tool
python -m pip install -r requirements.txt
```

## 4. 启动服务器和 Web 后台

服务器同时提供 ESP01S TCP 接收和 Web 后台：

```powershell
cd server
python server.py --host 0.0.0.0 --port 9000 --web-port 8080
```

浏览器打开：

```text
http://127.0.0.1:8080/
```

主要页面：

- `/devices`：设备列表、在线状态、心跳、温度、固件信息
- `/records`：考勤记录，可按设备、工号、时间筛选
- `/blacklist`：黑名单 UID 管理，可启用/停用
- `/ota`：查看 OTA 包信息

服务器数据保存在 `server/data/attendance.db`，该目录是本地运行数据，不提交到仓库。

## 5. 配置设备联网

ESP01S 只适合使用 2.4 GHz WPA/WPA2 网络。不要使用仅 5 GHz 或 WPA3-only 的热点。

通过上位机设备配置页，或串口命令写入：

```text
CFG:SSID=<你的2.4G WiFi名称>
CFG:PWD=<你的WiFi密码>
CFG:HOST=<电脑局域网IP>|PORT=9000
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|TZ=8
```

查询联网状态：

```text
NET?
```

理想状态类似：

```text
NET:READY=1|CFG=1|UPLOAD=1|STATE=6|TEXT=TRANSPARENT|NTP=1|BL=3|DEV=1|HOST=<电脑IP>|PORT=9000|SSID=<WiFi名称>
```

其中 `READY=1` 表示 TCP 透明传输可用，`NTP=1` 表示已校时，`BL` 是本地缓存的黑名单数量。

## 6. 启动上位机

开发方式启动：

```powershell
cd pc_tool
python run.py
```

打包成单文件 exe：

```powershell
cd pc_tool
.\build_exe.ps1
```

生成文件位于：

```text
pc_tool/dist/NFCAttendanceTool.exe
```

上位机主要功能：

- 打开串口连接设备
- 读取卡 UID
- 发普通卡、图像卡、管理员卡
- 写入账户头、头像、姓名、部门数据
- 清卡
- 导入下位机考勤记录到 SQLite
- 导出 CSV
- 配置设备号、考勤模式、WiFi、服务器、天气、时区

## 7. 发卡流程

1. 将卡放到 RC522 天线区域。
2. 在上位机连接串口。
3. 点击读卡，确认 UID。
4. 填写工号、类型、积分等信息。
5. 普通卡写入账户头；图像卡还会写入头像 24 块、姓名 10 块、部门 10 块。
6. 上位机发送 `UPDATEIMG` 完成图像数据提交。
7. 设备写卡前会再次读取当前 UID，若中途换卡会拒绝写入。

卡类型：

- `0`：普通考勤卡
- `1`：图像/信息卡
- `2`：管理员卡

## 8. 考勤模式

设备配置 `MODE` 控制考勤行为：

- `MODE=1`：入场模式，只记 IN，已经在场的卡拒绝为重复入场
- `MODE=2`：离场模式，只记 OUT，没有入场记录的卡拒绝为无入场
- `MODE=3`：进出交替，同一 UID 自动 IN/OUT/IN 交替
- `MODE=0`：普通打卡，记录类型为 NORMAL

离场会计算在场时长 `DUR=<秒>`，使用 Unix 秒差值，支持跨天。

## 9. OLED、LED 和蜂鸣器

OLED 使用 `SimSun_24` 分两页显示真实刷卡结果：

- 第 1 页：时间、类型、工号
- 第 2 页：`STAT <状态>` 和结果，例如 `OK`、`BLACKLIST`、`NO ENTRY`

刷卡详情显示期间：

- K3：下一页
- K2：上一页

LED 和蜂鸣器用于区分成功、失败、重复、网络在线/异常等反馈。

## 10. 管理员模式

发一张 `card_type=2` 的管理员卡后，现场刷管理员卡进入设置界面。

管理员模式功能：

- 120 秒无操作自动退出
- 普通卡在管理员模式期间被拒绝，不写考勤
- K2/K3 切换设备编号和考勤模式字段
- K4/K5 修改当前字段
- K1 退出
- K6 保存配置到 W25Q128，并重启后生效

## 11. 黑名单

Web 后台 `/blacklist` 可以添加或停用黑名单 UID。设备每次心跳后发送 `BL?` 拉取服务器启用的黑名单，最多缓存 16 个 UID。

协议示例：

```text
BL?
BL:COUNT=2|UIDS=A1B2C3D4,11223344
```

刷卡时如果 UID 命中本地黑名单，设备会在写考勤记录之前拒绝：

```text
ATTEND:ERR:BLACKLIST
```

OLED 显示：

```text
STAT ERR
BLACKLIST
```

该记录不会作为正常考勤写入 Flash，也不会作为正常考勤上传服务器。

## 12. Flash 离线存储

W25Q128 使用 LittleFS，保存：

- `/config.bin`：设备配置
- `/records.bin` 和 `/records.meta`：考勤记录环形存储
- `/weather.txt`：天气缓存
- `/ota.bin`、`/ota.meta`、`/bootstate.bin`：OTA 和 A/B 启动状态

考勤记录默认容量 1024 条，满后回卷覆盖最早记录。上传成功后记录标记为 `UP=DONE`，断网时保留 `UP=PENDING`，网络恢复后自动补传。

## 13. A/B OTA

当前固件支持 A/B 分区 OTA：

- Bootloader：`0x08000000..0x0800FFFF`
- Slot A：`0x08010000..0x0803FFFF`
- Slot B：`0x08040000..0x0807FFFF`

编译 A/B 应用：

```powershell
cd firmware/stm32/NFCAttend_Base
make APP_SLOT=A -j4
make APP_SLOT=B -j4
```

编译 bootloader：

```powershell
cd firmware/stm32/NFCAttend_Bootloader
make -j4
```

服务器 OTA 包路径：

```text
server/data/ota/current_A.bin
server/data/ota/version_A.txt
server/data/ota/current_B.bin
server/data/ota/version_B.txt
```

设备当前运行 A 时请求 `OTA?SLOT=B`，当前运行 B 时请求 `OTA?SLOT=A`。下载完成后串口发送：

```text
OTARST
```

bootloader 安装目标槽并重启。安装成功后 `OTA?` 会显示 `CUR=A/B`、`TARGET=<地址>`、`INSTALL=3|ERR=0`。

## 14. 编译和烧录

直接运行应用固件：

```powershell
cd firmware/stm32/NFCAttend_Base
make -j4
```

A/B 方式推荐先烧 bootloader，再烧 Slot A 应用：

```powershell
cd firmware/stm32/NFCAttend_Bootloader
make -j4

cd ..\NFCAttend_Base
make APP_SLOT=A -j4
```

OpenOCD 烧录命令示例：

```powershell
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build_app_a/Demo_W25Q128_AppA.elf verify reset exit"
```

具体 OpenOCD 路径按本机安装位置调整。

## 15. 常用串口命令

```text
PING                 测试串口
READ                 读取当前卡
ISSUE:<...>          发卡
CLEAR:<uid>          清卡
LIST:1               读取最近 1 条记录
LIST:ALL             读取全部记录
CFG?                 读取配置
NET?                 读取联网状态
TIME?                读取设备时间
WEATHER?             读取天气缓存
DIAG?                RC522 诊断
OTA?                 OTA 状态
OTA!                 主动查询/下载 OTA
OTARST               OTA 包准备好后重启安装
```

更完整协议见 [docs/protocol.md](docs/protocol.md)。

## 16. 验证命令

运行 PC 工具和服务器 Python 检查：

```powershell
python -m compileall pc_tool server
python pc_tool\tests\test_core.py
```

运行固件 host 测试：

```powershell
python firmware\app\tests\run_host_tests.py
```

运行汇总验证：

```powershell
python tools\run_verification.py
```

## 17. 当前状态

已实现并验证的主功能包括：

- RC522 稳定读 UID
- 普通卡、图像卡、管理员卡发卡
- 入场、离场、进出交替模式
- 离场在场时长计算，支持跨天
- OLED 两页刷卡详情和 K2/K3 翻页
- 管理员现场设置模式
- W25Q128 记录持久化、回卷和补传
- ESP01S WiFi、NTP、心跳、上传、天气
- Web 后台设备、记录、黑名单、OTA 页面
- 服务器黑名单下发和本地拒刷
- STM32 内部温度上报
- A/B 分区 OTA 实板闭环验证
