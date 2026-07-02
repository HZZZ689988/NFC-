---
type: evidence
target: board-bringup
date: 2026-07-01
hardware-validated: partial
---

# Board Bring-Up Log

## Setup

- Board: STM32F407VET6 NFC attendance board.
- Programmer: CMSIS-DAP over SWD.
- Programmer USB device: `VID_C251&PID_F001`, serial `0001A0000000`.
- Debug UART: same USB composite device, `COM3`.
- UART settings: `115200 8N1`.
- Firmware image: `firmware/stm32/NFCAttend_Base/build/Demo_W25Q128.elf`.
- Flash tool: xPack OpenOCD `0.12.0-7`.

The USB composite device exposes both:

- CMSIS-DAP HID interface for SWD download/debug.
- USB serial interface `COM3` for firmware USART1 communication.

## Download

Command:

```powershell
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/Demo_W25Q128.elf verify reset exit"
```

Observed result:

```text
Info : CMSIS-DAP: FW Version = 2.0.0
Info : SWD DPIDR 0x2ba01477
Info : [stm32f4x.cpu] Cortex-M4 r0p1 processor detected
Info : device id = 0x101f6413
Info : flash size = 512 KiB
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

Result: firmware download, verify and reset passed.

## Boot Log

After reset via OpenOCD while listening on `COM3`:

```text
Attendance app ready
NFC Attendance app started
K3=status K6=storage bootstrap L1=OK L2=invalid L3=duplicate L4=fault L5=network
```

## Serial Protocol Checks

Commands were sent as CRC frames over `COM3`.

| Command payload | Sent frame | Response |
| --- | --- | --- |
| `PING` | `$PING*6427\n` | `OK:PONG\n` |
| `CFG?` | `$CFG?*8D6A\n` | `CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=|PORT=0|TZ=8|SSID=|WLOC=\n` |
| `LIST:1` | `$LIST:1*7632\n` | `LIST:COUNT=0\nLIST:END\n` |
| `READ` | `$READ*DC73\n` | `ERR:NO_CARD\n` |

Result:

- USART1 command channel is working through `COM3`.
- CRC-framed protocol dispatch is working.
- Default device config can be loaded from storage through `CFG?`.
- Record listing path is working with an empty record file/state.
- RC522 no-card path is working at protocol level.

## Not Yet Covered

- W25Q128 JEDEC ID was not directly captured in this session. The firmware prints it from the K6 storage-bootstrap path.
- RC522 UID read with a real card was not tested.
- Card issue/readback/clear was not tested.
- OLED, LEDs, buzzer and ESP01S network paths were not tested.
- The known board-level risk remains: active RC522 column A maps `MOSI` to `PC4`, while the inherited base project also labels `PC4` as W25Q128 CS. RC522 and W25Q128 must still be validated together under real card operations.

## Storage Bootstrap Follow-Up

The K6 storage-bootstrap path was run from the board and printed:

```text
W25Q128 ID: 0xEF17
LittleFS storage ready
Device config: id=1 mode=3 upload=1 repeat=60
```

Result:

- W25Q128 JEDEC ID matches the expected `0xEF17`.
- LittleFS mounted successfully.
- Persistent config was readable from W25Q128/LittleFS.

## ESP01S Network Follow-Up

Test setup:

- WiFi SSID: `abc`
- WiFi password: `abc123456`
- PC WLAN IPv4: `192.168.107.234`
- Test TCP server: `python server/server.py --host 0.0.0.0 --port 9000`
- Firmware config written through `COM3`:

```text
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|TZ=8
CFG:SSID=abc
CFG:PWD=abc123456
CFG:HOST=192.168.107.234|PORT=9000
CFG:WKEY=|WLOC=hangzhou
```

Readback through `CFG?`:

```text
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
```

After reset, the board printed:

```text
[ESP01S] NTP授时成功(UDP): 2026-07-01 19:27:07
[ESP01S] RTC已校准: 2026-07-01 19:27:13
RTC synced from ESP01S NTP
ESP01S network ready
```

The TCP server accepted a connection from the ESP01S and recorded:

```text
HEARTBEAT:DEV=1
```

Result:

- Segmented `CFG:` writes persisted to LittleFS and were readable after write.
- ESP01S connected to the `abc` 2.4 GHz WiFi network.
- UDP NTP sync succeeded and calibrated the STM32 RTC.
- TCP connection to `server/server.py` succeeded.
- Heartbeat upload path reached the test server.

Still not covered:

- A real attendance `UPLOAD:` record and `ACK:UPLOAD:<seq>` round trip.
- Weather query/cache, because no weather API key was configured.

## RC522 Diagnostic Follow-Up

Firmware was rebuilt with the `DIAG?` command and downloaded again with
OpenOCD. Download and verify passed:

```text
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

After reset, ESP01S still reached the network:

```text
[ESP01S] NTP授时成功(UDP): 2026-07-01 19:49:57
[ESP01S] RTC已校准: 2026-07-01 19:50:03
RTC synced from ESP01S NTP
ESP01S network ready
```

CRC-framed checks over `COM3`:

| Command payload | Sent frame | Response |
| --- | --- | --- |
| `PING` | `$PING*6427\n` | `OK:PONG\n` |
| `DIAG?` | `$DIAG?*6DF3\n` | `DIAG:RC522_VER=0x00|TX=0x00|ERR=0x00|REQ=-1|TAG=0000\n` |
| `READ` | `$READ*DC73\n` | `ERR:NO_CARD\n` |

The same `DIAG?` and `READ` sequence was repeated and returned the same result.

Result:

- `COM3` protocol dispatch is still working.
- The RC522 diagnostic command is available on the board.
- `RC522_VER=0x00` and `TX=0x00` mean the MCU is not communicating with the
  RC522 chip on the configured pins.
- The result does not validate UID reading. It points to RC522 power/orientation,
  RST/NSS/SCK/MOSI/MISO wiring, or the known `PC4` conflict with W25Q128 CS.

## BSP Demo_RC522 Comparison

Source material:

- Local archive: `D:\c_work\BSP.rar`
- Extracted files checked under: `D:\c_work\_tmp_bsp_demo`
- Board reference: `https://gitee.com/zalileo/HX32F4-Board`

The archive contains `Demo_RC522` plus shared BSP files:

- `Bsp/NFC/rc522.c`
- `Bsp/NFC/rc522.h`
- `Bsp/NFC/rc522_platform_stm32.c`
- `Demo_RC522/Core/Src/freertos.c`
- `Demo_RC522/Core/Src/gpio.c`
- `Demo_RC522/Core/Inc/main.h`

Demo flow:

```text
UartDrv_Init(USART1)
RC522_Platform_Init()
RC522_ConfigISOType('A')
loop every 500 ms:
  RC522_ScanCard()
  RC522_ReadAllSectors()
```

Demo RC522 pin map:

```text
NSS  -> PE15
RST  -> PB15
MOSI -> PA0
MISO -> PB13
SCK  -> PD9
GND  -> PB10 software-controlled low output
```

Current attendance firmware pin map is intentionally different because the
actual board uses the Ethernet expansion header column:

```text
NSS  -> PB13
SCK  -> PB11
MOSI -> PC4
MISO -> PA1
RST  -> PA2
GND  -> GND
3.3V -> 3.3V
```

Software differences checked:

- Register read/write format matches Demo_RC522:
  `write = (addr << 1) & 0x7E`, `read = ((addr << 1) & 0x7E) | 0x80`.
- Reset and ISO14443A configuration sequence matches Demo_RC522.
- Software SPI is the same Mode 0 style: SCK idle low, MOSI set before SCK high,
  MISO sampled while SCK is high.
- Current firmware now explicitly reconfigures RC522 GPIOs inside
  `RC522_Platform_Init()` so PA1/MISO cannot remain in ADC analog mode.
- Current firmware serializes RC522 and W25Q128 access with `BoardSpiBus`.
- Current firmware reports extra diagnostics: raw version register, configured
  version register, command/IRQ/FIFO/TX/error registers, GPIO pin snapshot and
  `PC4` sharing status.

Latest flashed diagnostic after these changes:

```text
PING -> OK:PONG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
DIAG? -> DIAG:RC522_RAW=0x00|RC522_VER=0x00|CMD=0x00|IRQ=0x00|FIFO=0x00|TX=0x00|ERR=0x00|PINS=0x35|SHARE=1|REQ=-1|TAG=0000
READ -> ERR:NO_CARD
```

The same image was rebuilt, downloaded by board DAP/CMSIS-DAP, verified, reset
and tested again from `COM3` after adding the shared-bus guard:

```text
PING -> OK:PONG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
DIAG? -> DIAG:RC522_RAW=0x00|RC522_VER=0x00|CMD=0x00|IRQ=0x00|FIFO=0x00|TX=0x00|ERR=0x00|PINS=0x35|SHARE=1|REQ=-1|TAG=0000
READ -> ERR:NO_CARD
```

Interpretation:

- `PINS=0x35` means NSS high, SCK low, MOSI high, RST high and W25Q CS high;
  MISO is read low.
- `SHARE=1` confirms `PC4` is both RC522 MOSI and W25Q128 CS on this board.
- `RC522_RAW=0x00` before ISO configuration and `RC522_VER=0x00` after ISO
  configuration mean the MCU still receives all zero bits from RC522 SPI reads.
- With the Demo_RC522 SPI logic matched, the remaining failure is below the
  high-level driver flow: RC522 is not driving the configured MISO path, not
  selected/reset as expected, not powered/oriented as expected, or the selected
  board header mapping does not reach the RC522 module pins.
- The board DAP exposes both SWD download/debug and `COM3`; OpenOCD programming
  and serial command validation can run over the same Type-C connection.

## RC522-Only Diagnostic Firmware

A dedicated RC522-only build mode was added and tested:

```text
make clean
make RC522_ONLY_DIAG=1
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/RC522_Only_Diag.elf verify reset exit"
```

This firmware bypasses the attendance app, W25Q128/LittleFS bootstrap, ESP01S,
OLED and FreeRTOS tasks. It initializes only GPIO, DMA, USART1 and the RC522
platform, then prints the RC522 registers once per second.

Result captured from `COM3`:

```text
RC522_ONLY:RAW=0x00|VER=0x00|CMD=0x00->0x00|IRQ=0x00->0x00|FIFO=0x00->0x00|TX=0x00->0x00|ERR=0x00->0x00|PINS=0x35->0x35|SHARE=1|REQ=255|TAG=0000|SCAN=255|UID=00000000
RC522_ONLY:RAW=0x00|VER=0x00|CMD=0x00->0x00|IRQ=0x00->0x00|FIFO=0x00->0x00|TX=0x00->0x00|ERR=0x00->0x00|PINS=0x35->0x35|SHARE=1|REQ=255|TAG=0000|SCAN=255|UID=00000000
RC522_ONLY:RAW=0x00|VER=0x00|CMD=0xFF->0x00|IRQ=0xFF->0x00|FIFO=0xFF->0x00|TX=0xFF->0x00|ERR=0xFF->0x00|PINS=0x3D->0x35|SHARE=1|REQ=255|TAG=0000|SCAN=255|UID=00000000
RC522_ONLY:RAW=0x00|VER=0x00|CMD=0x00->0x00|IRQ=0x00->0x00|FIFO=0x00->0x00|TX=0x00->0x00|ERR=0x00->0x00|PINS=0x35->0x35|SHARE=1|REQ=255|TAG=0000|SCAN=255|UID=00000000
```

Interpretation:

- The RC522-only image runs and USART1 output is visible on `COM3`.
- Removing the attendance app, storage, network, display and scheduler tasks
  does not make the RC522 version register readable.
- `VER=0x00` still means the MCU is not receiving a valid response from RC522.
- The remaining validation should be electrical: scope/logic-analyzer
  `PB13/NSS`, `PB11/SCK`, `PC4/MOSI`, `PA1/MISO`, `PA2/RST`, confirm module
  3.3 V/GND/orientation, and resolve the `PC4` shared W25Q128-CS risk.

## RC522 Remap With Controlled PB10 Ground

The RC522 firmware mapping was changed to match the available board header:

```text
RC522 VCC        -> 3.3V
RC522 GND        -> PB10, configured as push-pull output low
RC522 SDA/SS/CS  -> PE15
RC522 SCK        -> PD9
RC522 MOSI       -> PA0
RC522 MISO       -> PB13
RC522 RST        -> PB15
RC522 IRQ        -> not connected
```

Implementation notes:

- `main()` now configures PB10 low immediately after `HAL_Init()`, before
  system clock and peripheral initialization.
- `MX_GPIO_Init()` and `RC522_Platform_Init()` both keep PB10 low and
  configure all RC522 pins for the remapped header.
- `RC522_Platform_ReadPins()` reports PB10 as bit `0x20`; a healthy controlled
  ground should leave that bit clear.
- `make RC522_ONLY_DIAG=1` now builds into `build_rc522_only/` so the
  diagnostic image cannot reuse stale normal-app objects when the macro changes.

Build/download commands used:

```text
make
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/Demo_W25Q128.elf verify reset exit"
make RC522_ONLY_DIAG=1
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build_rc522_only/RC522_Only_Diag.elf verify reset exit"
```

RC522-only result captured from `COM3`:

```text
RC522_ONLY:RAW=0x00|VER=0x00|CMD=0x00->0x00|IRQ=0x00->0x00|FIFO=0x00->0x00|TX=0x00->0x00|ERR=0x00->0x00|PINS=0x51->0x51|SHARE=0|REQ=255|TAG=0000|SCAN=255|UID=00000000
```

Final normal-app result after flashing `build/Demo_W25Q128.elf` back:

```text
DIAG? -> DIAG:RC522_RAW=0x00|RC522_VER=0x00|CMD=0x00|IRQ=0x00|FIFO=0x00|TX=0x00|ERR=0x00|PINS=0x51|SHARE=0|REQ=-1|TAG=0000
READ  -> ERR:NO_CARD
```

Interpretation:

- `SHARE=0` confirms the new MOSI pin `PA0` no longer shares W25Q128 CS `PC4`.
- `PINS=0x51` means NSS high, RST high, W25Q CS high, and PB10 controlled
  ground is not reading high.
- `RC522_VER=0x00` remains a communication failure before UID reading. The
  software mapping and BSP-compatible SPI sequence are now aligned with
  `Demo_RC522`; the remaining issue is below the RC522 driver layer, most likely
  PB10-as-ground electrical margin, module power/orientation, or the board
  header path to RC522 MISO/CS/SCK/MOSI/RST.

## OLED 12px GBK Font Test

The OLED Chinese diagnostic was changed to use the generated `SimSun_12.c`
subset instead of the full `SimSun_8.c` font:

- The firmware now links `../Bsp/OLED/SimSun_12.c`.
- `OLEDTEST` draws ASCII reference text first, then switches to
  `GUI_FontHZ_SimSun_12`.
- Chinese glyphs are drawn by direct GBK character codes, not by passing
  UTF-8 source strings through the uncertain conversion path.
- Chinese spacing was increased to 16 pixels per character.

Expected OLED test content after sending `OLEDTEST`:

```text
OLED FONT TEST
ASCII OK 012345
CMD:OLEDTEST
12PX GBK
杭电科技大学
曾州子毓
```

Build/download commands used:

```text
python firmware\app\tests\run_host_tests.py
make -j4
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/Demo_W25Q128.elf verify reset exit"
```

Results:

```text
Host tests: passed
Firmware build: passed, text=102536 data=496 bss=46496
OpenOCD: Programming Finished, Verified OK, Resetting Target
COM3 startup: Attendance app ready, ESP01S network ready
OLEDTEST -> OK:OLEDTEST
```

The generated 12px GBK dot matrix was also parsed locally for the test codes
`babc b5e7 bfc6 bcbc b4f3 d1a7 d4f8 d6dd d7d3 d8b9`; the glyph bitmaps contain
recognizable stroke structures. If the physical OLED still shows unreadable
Chinese after this firmware, the next focus should be OLED bit order/scan
orientation or the generated font export settings, not the serial command path.

## OLED 16px GBK Font Iteration

Physical OLED feedback for the 12px font was: Chinese was recognizable, but
many strokes were missing and character error rate was high. This is consistent
with 12px Chinese glyphs being too low-resolution for reliable recognition.

The diagnostic was changed again to use the board demo `FangSong_16.c` subset:

- The firmware now links `../Bsp/OLED/FangSong_16.c`.
- `OLEDTEST` keeps the first three rows as 8px ASCII reference text.
- The label `16PX GBK` is drawn with the default 8px font.
- The two Chinese rows are drawn with `GUI_FontHZ_FangSong_16` at y=32 and
  y=48, using direct GBK codes.

Expected OLED test content:

```text
OLED FONT TEST
ASCII OK 012345
CMD:OLEDTEST
16PX GBK
杭电科技大学
曾州子毓
```

Software validation:

```text
Host tests: passed
Firmware build: passed, text=105760 data=496 bss=46496
16px GBK dot-matrix parse: glyphs are visibly more complete than 12px
```

Initial download attempt was blocked by temporary USB device enumeration loss,
then the board reappeared as `COM3` and CMSIS-DAP programming succeeded:

```text
openocd.exe -f .\openocd.cfg -c "adapter speed 1000" -c "program build/Demo_W25Q128.elf verify reset exit"
OpenOCD: Programming Finished, Verified OK, Resetting Target
```

`OLEDTEST` was sent over `COM3` after flashing:

```text
OLEDTEST -> OK:OLEDTEST
[ESP01S] RTC sync...
ESP01S network ready
```

The 16px font firmware is now running on the board. The physical display should
be checked for whether the larger font resolves the missing-stroke issue.

## OLED Demo-Compatible GBK Path

The official EX07 OLED demo was flashed and its Chinese text displayed normally.
After porting the same OLED driver stack into the attendance firmware, the most
stable test path is:

```c
GUI_SetFont(&GUI_FontHZ_SimSun_24);
GUI_DispStringAt("\xc4\xfa\xba\xc3\xa3\xa1\nOLED", 0, 0);
```

This byte string is GBK for `您好！`. It is intentionally written as escaped
bytes, not as UTF-8 source text, because the BSP GUI parser combines bytes above
`0x7f` into GBK-style two-byte character codes. Passing UTF-8 Chinese source
strings makes the parser read the wrong code points and produces mojibake.

Board feedback after this change: the characters are broadly normal. This
matches the software analysis:

- OLED hardware, software I2C, SSD1306 refresh and the GUI string path are
  working.
- The bundled `SimSun_24.c` is only a demo subset. It contains ASCII plus
  `您好！杭州电子科技大学曾毓子`, not a full Chinese font.
- Chinese outside that subset will still render as missing/incorrect glyphs
  unless a matching GBK font subset is generated and linked.

Current decision: keep the production status page in ASCII for now, and use
`OLEDTEST` as a known-good Chinese diagnostic. Full Chinese business pages
should be enabled only after generating a GBK font subset that covers the actual
attendance UI words.

## USART1 And ESP01S Regression, RC522 Paused

Date: 2026-07-02.

Scope: RC522 was intentionally not tested. This pass verified the non-RC522
paths after switching the OLED production page to sparse 24px text.

Finding: `COM3` receive initially answered short `PING` commands, then stopped
responding after `CFG?`. The failure was reproduced after reset. The firmware
was changed to:

- avoid the UART driver's asynchronous `HAL_UART_AbortReceive_IT()` startup
  race before `HAL_UARTEx_ReceiveToIdle_IT()`;
- increase `serialTask` stack from 2 KB to 4 KB for protocol formatting and
  storage calls;
- enable FreeRTOS stack overflow checking.

Validation after flashing:

```text
OpenOCD: Programming Finished, Verified OK, Resetting Target
PING -> OK:PONG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
PING -> OK:PONG
LIST:1 -> LIST:COUNT=0 / LIST:END
LIST:ALL -> LIST:COUNT=0 / LIST:END
OLEDTEST -> OK:OLEDTEST
PING -> OK:PONG
```

Network validation used the local test server on `192.168.107.234:9000`:

```text
python server.py --host 0.0.0.0 --port 9000
```

Board serial output after reset:

```text
[ESP01S] NTP sync success
RTC synced from ESP01S NTP
ESP01S network ready
```

Server log evidence:

```text
2026-07-02T12:41:42  192.168.107.122:8443  HEARTBEAT:DEV=1
```

Conclusion: DAP flashing, USART1 bidirectional command handling, W25Q128-backed
empty record listing, OLED `OLEDTEST`, ESP01S WiFi/NTP/RTC/TCP startup and
heartbeat-to-server are validated. Real attendance upload remains blocked until
RC522 card input is resumed or a synthetic record injection path is added.

## Simulated Attendance Upload Closure

Date: 2026-07-02.

RC522 remained paused. A `SIMATT:<uid>,<sid>,<type>` serial command was added to
generate a pending attendance record through the application layer, so the
network upload path can be validated without card input.

Validation:

```text
SIMATT:A1B2C3D4,1001,2 -> OK:SIMATT:SEQ=1
LIST:3 ->
  REC:SEQ=1|UID=A1B2C3D4|SID=1001|NORMAL|1782996646|DEV=1|OK|UP=DONE
```

A second record showed the full pending-to-done transition:

```text
SIMATT:A1B2C3D5,1002,2 -> OK:SIMATT:SEQ=2
LIST:3 ->
  REC:SEQ=2|UID=A1B2C3D5|SID=1002|NORMAL|1782996812|DEV=1|OK|UP=PENDING
```

Server log:

```text
2026-07-02T12:53:35  192.168.107.122:29145  UPLOAD:SEQ=2|UID=A1B2C3D5|SID=1002|TYPE=2|TS=1782996812|DEV=1
```

After the server returned `ACK:UPLOAD:2`, the board reported:

```text
REC:SEQ=2|UID=A1B2C3D5|SID=1002|NORMAL|1782996812|DEV=1|OK|UP=DONE
```

Conclusion: record append, pending upload selection, ESP01S transparent TCP
send, server ACK, `att_storage_mark_uploaded()` and LittleFS persistence of the
upload state are board-validated without RC522.

## OLED And Feedback Command Harness

Date: 2026-07-02.

RC522 remained paused. A `UITEST:<case>` serial command was added to trigger the
same display and feedback paths used by real attendance and network events.

Validated over `COM3` after flashing:

```text
UITEST:READY   -> OK:UITEST
UITEST:OK      -> OK:UITEST
UITEST:DUP     -> OK:UITEST
UITEST:INVALID -> OK:UITEST
UITEST:ERROR   -> OK:UITEST
UITEST:NETOK   -> OK:UITEST
UITEST:NETERR  -> OK:UITEST
UITEST:BAD     -> ERR:ARG
PING           -> OK:PONG
```

Expected physical behavior:

- `READY`: OLED `TAP CARD` plus current network state.
- `OK`: OLED `OK` / `ID1001`, L1 pulse and short success tone.
- `DUP`: OLED `DUP` / `WAIT`, L3 pulse and short duplicate tone.
- `INVALID`: OLED `BAD CARD` / `CHECK`, L2 pulse and invalid-card tone.
- `ERROR`: OLED `ERROR` / `TEST`, L4 pulse and fault tone.
- `NETOK`: ready OLED network line changes to `NET OK`, L5 pulse and network tone.
- `NETERR`: ready OLED network line changes to `NET ERROR`, L4 pulse and fault tone.

Host tests now cover the `UITEST` command routing and the sparse 24px OLED event
pages. Physical OLED/LED/buzzer observation still requires watching the board
during the command sequence.

### UITEST Serial Pass

Date: 2026-07-02.

The board accepted the full `UITEST` command sequence over `COM3`:

```text
UITEST:READY   -> OK:UITEST
UITEST:OK      -> OK:UITEST
UITEST:DUP     -> OK:UITEST
UITEST:INVALID -> OK:UITEST
UITEST:ERROR   -> OK:UITEST
UITEST:NETOK   -> OK:UITEST
UITEST:NETERR  -> OK:UITEST
UITEST:READY   -> OK:UITEST
```

The event-hold behavior was also exercised:

```text
UITEST:OK -> OK:UITEST
wait about 6 seconds
PING      -> OK:PONG
UITEST:READY -> OK:UITEST
```

After a DAP reset, persisted config and uploaded records were still available:

```text
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
LIST:COUNT=2
REC:SEQ=1|UID=A1B2C3D4|SID=1001|NORMAL|1782996646|DEV=1|OK|UP=DONE
REC:SEQ=2|UID=A1B2C3D5|SID=1002|NORMAL|1782996812|DEV=1|OK|UP=DONE
LIST:END
PING -> OK:PONG
```

This confirms the command-level UI/feedback harness and LittleFS persistence
after reset. Physical OLED/LED/buzzer pass/fail still depends on visual/audio
observation during the sequence above.

User physical observation on 2026-07-02: buzzer sound, OLED pages and LED
mapping were approximately correct and matched expectations.

## RTC Timestamp And Upload Retention Check

Date: 2026-07-02.

RC522 remained paused. The board was reset through CMSIS-DAP while `COM3` stayed
open. A simulated attendance record was injected as soon as the application
banner appeared and before the next ESP01S NTP/RTC sync log line.

Pre-NTP command and readback:

```text
SIMATT:A1B2C3D8,1005,2 -> OK:SIMATT:SEQ=5
LIST:10 ->
  REC:SEQ=5|UID=A1B2C3D8|SID=1005|NORMAL|1782998227|DEV=1|OK|UP=PENDING
```

The fresh NTP sync then completed:

```text
[ESP01S] NTP...: 2026-07-02 13:17:19
[ESP01S] RTC...: 2026-07-02 13:17:25
RTC synced from ESP01S NTP
ESP01S network ready
```

Post-NTP command and readback:

```text
SIMATT:A1B2C3D9,1006,2 -> OK:SIMATT:SEQ=6
LIST:10 ->
  REC:SEQ=6|UID=A1B2C3D9|SID=1006|NORMAL|1782998246|DEV=1|OK|UP=PENDING
```

After the upload worker received server ACKs, both records were persisted as
uploaded:

```text
REC:SEQ=5|UID=A1B2C3D8|SID=1005|NORMAL|1782998227|DEV=1|OK|UP=DONE
REC:SEQ=6|UID=A1B2C3D9|SID=1006|NORMAL|1782998246|DEV=1|OK|UP=DONE
```

Server evidence:

```text
2026-07-02T13:17:28  192.168.107.122:33534  UPLOAD:SEQ=5|UID=A1B2C3D8|SID=1005|TYPE=2|TS=1782998227|DEV=1
2026-07-02T13:17:39  192.168.107.122:33534  UPLOAD:SEQ=6|UID=A1B2C3D9|SID=1006|TYPE=2|TS=1782998246|DEV=1
```

Conclusion: after a DAP reset, `AttendanceTime_Now()` returned RTC-derived Unix
seconds immediately, before the next NTP sync finished. Simulated attendance
records were appended, read back through the CRC-checked LittleFS record path,
uploaded, ACKed and marked `UP=DONE`. This verifies DAP-reset RTC continuity; it
does not replace a full power-loss/VBAT retention test.

## Weather Cache And OLED Page Harness

Date: 2026-07-02.

RC522 remained paused. A short weather diagnostic was added so the LittleFS
weather cache and sparse 24px OLED weather page can be validated without a real
weather API key.

Host verification:

```text
python firmware/app/tests/run_host_tests.py -> passed
python pc_tool/tests/test_core.py -> passed
make -j4 -> passed, text=105096 data=496 bss=44360
```

Flash verification:

```text
program build/Demo_W25Q128.elf verify reset exit -> Verified OK
```

Board command pass:

```text
PING -> OK:PONG
WEATHERTEST:Sunny 20C -> OK:WEATHERTEST
WEATHER? -> WEATHER:Sunny 20C
```

`WEATHERTEST` writes `/weather.txt`, updates the display model and switches OLED
to the two-line 24px page:

```text
WEATHER
Sunny 20C
```

After a DAP reset, the cached weather was still available and the device config
remained intact:

```text
WEATHER? -> WEATHER:Sunny 20C
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
```

The forced real query command was also exercised without a weather API key:

```text
WEATHER! -> ERR:NOT_READY
```

Conclusion: LittleFS `/weather.txt` creation/update, serial readback, startup
reload and the compact 24px OLED weather page are board-validated. The real
ESP01S weather API path still needs a valid `WKEY` before it can be marked as
hardware-validated.

## Time Source Diagnostic Command

Date: 2026-07-02.

RC522 remained paused. A `TIME?` serial command was added so DAP-reset and later
full power-loss/VBAT RTC checks can read the same application time source used
for attendance records.

Host verification:

```text
python firmware/app/tests/run_host_tests.py -> passed
python pc_tool/tests/test_core.py -> passed
python -m compileall pc_tool server -> passed
make -j4 -> passed, text=105208 data=496 bss=44360
```

Flash verification:

```text
program build/Demo_W25Q128.elf verify reset exit -> Verified OK
```

Board command pass:

```text
TIME? -> TIME:1783011177|VALID=1
WEATHER? -> WEATHER:Sunny 20C
```

Conclusion: `TIME?` now reports current Unix seconds plus a validity flag.
`VALID=1` means the app time source is RTC/NTP-derived Unix time; `VALID=0`
would indicate RTOS uptime fallback. This makes the remaining full power-loss
/ VBAT RTC retention test directly observable over `COM3`.

## Power-Cycle Time And Weather Location Check

Date: 2026-07-02.

The user reported that the board had been power-cycled once. After reconnecting
to `COM3`, the running attendance firmware responded to diagnostics:

```text
TIME? -> TIME:1783011751|VALID=1
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=hangzhou
```

The weather implementation uses the Seniverse daily forecast endpoint:

```text
GET /v3/weather/daily.json?key=<WKEY>&location=<WLOC>&language=en&unit=c&start=0&days=1
Host: api.seniverse.com
```

The official Seniverse daily weather documentation shows the same query
parameter shape with `key`, `location`, `language`, `unit`, `start` and `days`.
The Seniverse common API parameters define coordinate locations as
`latitude:longitude`. Hangzhou was configured as a compact coordinate location:

```text
CFG:WKEY=|WLOC=30.267:120.153 -> OK:CFG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
WEATHER! -> ERR:NOT_READY
TIME? -> TIME:1783011952|VALID=1
```

Conclusion: after the reported power cycle, the application time source still
returned a valid Unix timestamp over `TIME?`, and the board now persists the
Hangzhou weather location as `30.267:120.153`. `WEATHER!` correctly returned
`ERR:NOT_READY` because `WKEY` is intentionally empty. A real weather query
still requires a private Seniverse API key from the user's own account; no
public key can be safely or legitimately substituted.

## Real Seniverse Weather Query

Date: 2026-07-02.

The user provided a private Seniverse API key. The key was used only for local
validation and is not recorded in full in this repository.

PC-side API probe:

```text
GET /v3/weather/daily.json?key=<redacted>&location=30.267:120.153&language=en&unit=c&start=0&days=1
Location=Hangzhou
TextDay=Light rain
High=29
Low=23
Date=2026-07-02
LastUpdate=2026-07-02T08:00:00+08:00
```

The first board run confirmed the key and location were not the problem but
exposed an ESP01S mode-transition issue:

```text
WEATHER! -> ERR:WEATHER
[ESP01S] weather CIPSEND not ready
WEATHER? -> WEATHER:Sunny 20C
```

The ESP01S weather path was then fixed to leave transparent TCP mode more
conservatively, switch back to `CIPMODE=0` before fixed-length HTTP send, wait
for the `AT+CIPSEND=<len>` `>` prompt before sending the HTTP GET, and increase
the weather HTTP receive buffer to 2048 bytes.

Verification:

```text
python firmware/app/tests/run_host_tests.py -> passed
python pc_tool/tests/test_core.py -> passed
make -j4 -> passed, text=105496 data=496 bss=45392
program build/Demo_W25Q128.elf verify reset exit -> Verified OK
```

Board command pass:

```text
[ESP01S] weather query success: Hangzhou day Light rain 29C night Light rain 23C precip 0.80
WEATHER! -> WEATHER:Light 23C
WEATHER? -> WEATHER:Light 23C
TIME? -> TIME:1783012906|VALID=1
```

Conclusion: the real ESP01S-to-Seniverse weather path is board-validated with
Hangzhou coordinates and the private API key. The result is cached in
LittleFS `/weather.txt`, read back by `WEATHER?`, and displayed through the
existing sparse 24px weather page.

## Upload Outage And Recovery Check

Date: 2026-07-02.

RC522 remained paused. The board was configured to a deliberately unreachable
TCP port to validate that attendance records remain stored when upload cannot
complete:

```text
CFG:HOST=192.168.107.234|PORT=8999 -> OK:CFG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=8999|TZ=8|SSID=abc|WLOC=30.267:120.153
SIMATT:A1B2C3EA,2101,2 -> OK:SIMATT:SEQ=7
LIST:3 ->
REC:SEQ=7|UID=A1B2C3EA|SID=2101|NORMAL|1783013204|DEV=1|OK|UP=PENDING
```

The configured server port was then restored while `server/server.py` was
running on `192.168.107.234:9000`:

```text
CFG:HOST=192.168.107.234|PORT=9000 -> OK:CFG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
```

After reconnection and the next upload poll, the same record changed to
`UP=DONE`:

```text
LIST:3 ->
REC:SEQ=7|UID=A1B2C3EA|SID=2101|NORMAL|1783013204|DEV=1|OK|UP=PENDING

LIST:3 ->
REC:SEQ=7|UID=A1B2C3EA|SID=2101|NORMAL|1783013204|DEV=1|OK|UP=DONE
```

Server log evidence:

```text
2026-07-02T17:27:47 192.168.107.122:25088 HEARTBEAT:DEV=1
2026-07-02T17:27:47 192.168.107.122:25088 UPLOAD:SEQ=7|UID=A1B2C3EA|SID=2101|TYPE=2|TS=1783013204|DEV=1
2026-07-02T17:27:48 192.168.107.122:25088 UPLOAD:SEQ=7|UID=A1B2C3EA|SID=2101|TYPE=2|TS=1783013204|DEV=1
```

Conclusion: while the server was unreachable, a simulated attendance record was
stored and remained pending. After restoring the server endpoint, the board sent
the pending `UPLOAD:` frame, received the test server ACK, and persisted
`UP=DONE` in LittleFS. This validates the non-RC522 offline retention and
recovery upload path.

## Config Persistence And Restore Check

Date: 2026-07-02.

RC522 remained paused. The current production configuration was first read as
the baseline:

```text
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
```

A deliberately non-default configuration was written in segmented `CFG:`
commands:

```text
CFG:DEV=42|MODE=2|UPLOAD=0|REPEAT=7|TZ=9 -> OK:CFG
CFG:SSID=abc -> OK:CFG
CFG:PWD=abc123456 -> OK:CFG
CFG:HOST=192.168.107.234|PORT=8998 -> OK:CFG
CFG:WKEY=|WLOC=test-hangzhou -> OK:CFG
CFG? -> CFG:DEV=42|MODE=2|UPLOAD=0|REPEAT=7|HOST=192.168.107.234|PORT=8998|TZ=9|SSID=abc|WLOC=test-hangzhou
```

After a CMSIS-DAP reset, the temporary configuration was still present:

```text
reset run -> OK
Attendance app ready
ESP01S network start failed: -3
CFG? -> CFG:DEV=42|MODE=2|UPLOAD=0|REPEAT=7|HOST=192.168.107.234|PORT=8998|TZ=9|SSID=abc|WLOC=test-hangzhou
```

The `ESP01S network start failed: -3` result was expected because the temporary
server port was intentionally unreachable. The production configuration was then
restored. The private weather key was rewritten but not recorded in this file:

```text
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|TZ=8 -> OK:CFG
CFG:SSID=abc -> OK:CFG
CFG:PWD=abc123456 -> OK:CFG
CFG:HOST=192.168.107.234|PORT=9000 -> OK:CFG
CFG:WKEY=<redacted>|WLOC=30.267:120.153 -> OK:CFG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
```

After a second CMSIS-DAP reset, the restored production configuration loaded and
the network/weather paths were healthy:

```text
reset run -> OK
Attendance app ready
RTC synced from ESP01S NTP
ESP01S network ready
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
WEATHER? -> WEATHER:Light 23C
```

Conclusion: `/config.bin` persists segmented device, mode, upload, repeat,
timezone, WiFi, server and weather-location settings across reset. Runtime
network configuration is reapplied after reset, and the board was returned to
the production configuration.

## PC Tool Serial Integration Check

Date: 2026-07-02.

The desktop upper-computer code path was validated against the live board by
using `pc_tool.nfc_attendance_tool.SerialClient` and the same protocol command
builders used by the Tkinter UI. This verifies the serial transaction
terminators and command formatting without requiring manual GUI clicks.

Host verification:

```text
python pc_tool/tests/test_core.py -> passed
python -m compileall pc_tool server -> passed
```

Board interaction through `COM3`:

```text
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
TIME? -> TIME:1783014212|VALID=1
WEATHER? -> WEATHER:Light 23C
WEATHERTEST:PC Link 21C -> OK:WEATHERTEST
WEATHER? -> WEATHER:PC Link 21C
WEATHER! -> WEATHER:Light 23C
WEATHER? -> WEATHER:Light 23C
LIST:2 ->
LIST:COUNT=7
REC:SEQ=6|UID=A1B2C3D9|SID=1006|NORMAL|1782998246|DEV=1|OK|UP=DONE
REC:SEQ=7|UID=A1B2C3EA|SID=2101|NORMAL|1783013204|DEV=1|OK|UP=DONE
LIST:END
```

Conclusion: the upper-computer protocol builders and serial client interoperate
with the board for config/time/weather diagnostics, weather cache writes, forced
real weather refresh and multi-line record listing. The temporary PC-link
weather text was restored to the real Seniverse weather cache before ending the
test.

## Non-RC522 Requirement Sweep With Substitutes

Date: 2026-07-02.

RC522 real-card I/O remained intentionally paused. RC522-dependent business
flows were substituted with `SIMATT`, no-card `READ`/`ISSUE`/`CLEAR` checks and
host mock tests so the rest of the firmware, storage, protocol, network and
upper-computer requirements could continue moving.

Code fix found during the sweep:

```text
Before fix:
REPEAT=60
SIMATT:A1B2C3EB,2102,2 -> OK:SIMATT:SEQ=8
SIMATT:A1B2C3EB,2102,2 -> OK:SIMATT:SEQ=9
```

The substitute attendance path was updated to use the same in-memory
anti-repeat state as local NFC polling. Host tests were added for duplicate
`SIMATT` rejection and same-UID acceptance after the repeat interval.

Verification:

```text
python tools/run_verification.py -> passed
make -j4 -> passed, text=105592 data=496 bss=45392
arm-none-eabi-readelf -l build/Demo_W25Q128.elf -> LOAD segments R E / RW / RW, no RWE
program build/Demo_W25Q128.elf verify reset exit -> Verified OK
```

Board anti-repeat substitute check:

```text
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|TZ=8 -> OK:CFG
SIMATT:A1B2C3EC,2201,2 -> OK:SIMATT:SEQ=10
SIMATT:A1B2C3EC,2201,2 -> ERR:DUPLICATE
LIST:5 ->
REC:SEQ=10|UID=A1B2C3EC|SID=2201|NORMAL|1783014647|DEV=1|OK|UP=PENDING
```

Only one `A1B2C3EC` record was appended, so the substitute path now exercises
the same anti-repeat requirement without RC522 hardware.

Board upload-disable check:

```text
CFG:DEV=1|MODE=3|UPLOAD=0|REPEAT=60|TZ=8 -> OK:CFG
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=0|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
SIMATT:A1B2C3ED,2202,2 -> OK:SIMATT:SEQ=11
LIST:5 ->
REC:SEQ=11|UID=A1B2C3ED|SID=2202|NORMAL|1783014648|DEV=1|OK|UP=PENDING
```

After restoring upload, pending records were retried and marked done:

```text
CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|TZ=8 -> OK:CFG
CFG:HOST=192.168.107.234|PORT=9000 -> OK:CFG
CFG:WKEY=<redacted>|WLOC=30.267:120.153 -> OK:CFG
LIST:5 ->
REC:SEQ=10|UID=A1B2C3EC|SID=2201|NORMAL|1783014647|DEV=1|OK|UP=DONE
REC:SEQ=11|UID=A1B2C3ED|SID=2202|NORMAL|1783014648|DEV=1|OK|UP=DONE
WEATHER? -> WEATHER:Light 23C
```

Board protocol and no-card checks:

```text
$PING*6427 -> OK:PONG
$PING*0000 -> ERR:CRC
LIST:N -> ERR:ARG
READ -> ERR:NO_CARD
ISSUE:A1B2C3EE,2301,0,0 -> ERR:NO_CARD
CLEAR:A1B2C3EE -> ERR:NO_CARD
```

Conclusion: with RC522 real-card I/O excluded, the remaining attendance
business path can be substituted through `SIMATT`: local record append,
anti-repeat rejection, pending upload retention, upload-enable gating, recovery
upload, CRC-framed command parsing, invalid-frame rejection, bad argument
rejection and no-card card-command rejection were all validated on the board.
The board was restored to production config.

## Upper-Computer SQLite And CSV Validation

Date: 2026-07-02.

RC522 remained paused. This pass closed the upper-computer local persistence
requirements that do not need real card RF I/O:

- People are inserted/updated in SQLite.
- Issue logs are inserted for card-writing actions.
- Lost-card marks can be set and cleared.
- Board `REC:` lines with `UP=PENDING/DONE/FAILED` are parsed into
  `upload_state`.
- Re-importing the same `DEV+SEQ` record refreshes upload state, for example
  `PENDING` to `DONE`, without creating a duplicate row.
- Existing SQLite databases are migrated by adding the `upload_state` column.
- Attendance CSV export uses UTF-8-SIG and includes the upload-state column.

Host verification:

```text
python pc_tool/tests/test_core.py -> passed
python -m compileall pc_tool server -> passed
python tools/run_verification.py -> passed
git diff --check -> passed
```

Conclusion: the upper-computer now preserves the upload status returned by the
board, displays it in the attendance-record table and exports it with the rest
of the attendance data. This validates the SQLite/CSV side of the
upper-computer requirements while RC522 card operations remain paused.

## Upper-Computer Live Import And CSV Export

Date: 2026-07-02.

RC522 remained paused. The desktop upper-computer modules were used against the
live board on `COM3` to read existing substitute attendance records, import them
into a temporary SQLite database and export a CSV.

Commands and board responses:

```text
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
TIME? -> TIME:1783015349|VALID=1
LIST:5 ->
LIST:COUNT=11
REC:SEQ=7|UID=A1B2C3EA|SID=2101|NORMAL|1783013204|DEV=1|OK|UP=DONE
REC:SEQ=8|UID=A1B2C3EB|SID=2102|NORMAL|1783014458|DEV=1|OK|UP=DONE
REC:SEQ=9|UID=A1B2C3EB|SID=2102|NORMAL|1783014458|DEV=1|OK|UP=DONE
REC:SEQ=10|UID=A1B2C3EC|SID=2201|NORMAL|1783014647|DEV=1|OK|UP=DONE
REC:SEQ=11|UID=A1B2C3ED|SID=2202|NORMAL|1783014648|DEV=1|OK|UP=DONE
LIST:END
```

Temporary database and CSV result:

```text
IMPORT: rec_count=5 rows=5
DBROW: 11 A1B2C3ED 2202 NORMAL 1 OK DONE
DBROW: 10 A1B2C3EC 2201 NORMAL 1 OK DONE
DBROW: 9 A1B2C3EB 2102 NORMAL 1 OK DONE
DBROW: 8 A1B2C3EB 2102 NORMAL 1 OK DONE
DBROW: 7 A1B2C3EA 2101 NORMAL 1 OK DONE
CSV_HEADER: seq|uid_hex|sid|record_type|occurred_at|device_id|status|upload_state|imported_at
CSV_FIRST: 11|A1B2C3ED|2202|NORMAL|1783014648|1|OK|DONE
```

Conclusion: the upper-computer serial client, record import parser, SQLite
storage and CSV export work together against the live board record stream. This
validates the PC-side attendance import/export path with RC522 substituted by
previously injected records.

## Image Command Boundary Regression

Date: 2026-07-02.

RC522 real-card I/O remained paused. The firmware image with protocol-level
image block index checks was built, downloaded and verified:

```text
make -j4 -> passed, text=105624 data=496 bss=45392
program build/Demo_W25Q128.elf verify reset exit -> Verified OK
arm-none-eabi-readelf -l build/Demo_W25Q128.elf -> LOAD segments R E / RW / RW, no RWE
```

Board command pass over `COM3`:

```text
PING -> OK:PONG
IMGN10:00112233445566778899AABBCCDDEEFF -> ERR:ARG
IMGA23:00112233445566778899AABBCCDDEEFG -> ERR:ARG
UPDATEIMG -> ERR:NOT_READY
CFG? -> CFG:DEV=1|MODE=3|UPLOAD=1|REPEAT=60|HOST=192.168.107.234|PORT=9000|TZ=8|SSID=abc|WLOC=30.267:120.153
TIME? -> TIME:1783015843|VALID=1
```

Conclusion: the firmware now rejects image-block commands with out-of-range
indexes or invalid 16-byte hex payloads before calling the card writer, and an
incomplete image update returns `ERR:NOT_READY`. This validates the non-RC522
protocol boundary for image-card commands; real Mifare writes still require
RC522 communication to be restored.

## RC522 Replacement Hardware Minimal Diagnostic

Date: 2026-07-02.

The user replaced the RC522 hardware. The RC522-only diagnostic firmware was
rebuilt and flashed again:

```text
make RC522_ONLY_DIAG=1 -j4 -> passed, text=108728 data=496 bss=46112
program build_rc522_only/RC522_Only_Diag.elf verify reset exit -> Verified OK
```

The active diagnostic/app pin map for this run was:

```text
RC522 VCC        -> 3.3V
RC522 GND        -> PB10, firmware-controlled low
RC522 NSS/SDA/CS -> PE15
RC522 SCK        -> PD9
RC522 MOSI       -> PA0
RC522 MISO       -> PB13
RC522 RST        -> PB15
RC522 IRQ        -> not connected
```

Serial output captured from `COM3`:

```text
RC522_ONLY:RAW=0xFF|VER=0xFF|CMD=0xFF->0xFF|IRQ=0xFF->0xFF|FIFO=0xFF->0xFF|TX=0xFF->0xFF|ERR=0xFF->0xFF|PINS=0x59->0x59|SHARE=0|SPD=0xFF->0xFF->0xFF|RW=0|MISO=0x03|DRVVER=00/00/00|MAP=DEMO=00/00/00,H1=FF/FF/FF,H2=FF/FF/FF|PERM=H1=none,H2=none|REQ=255|TAG=0000|SCAN=255|UID=00000000
```

Repeated lines were stable with the same values.

Interpretation:

- `VER=0xFF` is not a valid MFRC522 version value; expected healthy values are
  normally `0x91` or `0x92`.
- `RW=0` means the diagnostic write/read-back test did not work.
- `MISO=0x03` means the current MISO pin reads high with no pull and pull-up,
  but can be pulled low internally. That is consistent with MISO being idle or
  not driven by the RC522, rather than a valid SPI response.
- `MAP=...` and `PERM=H1=none,H2=none` mean the diagnostic also tried the known
  demo/H1/H2 candidate header maps and did not find a valid version register.
- `SCAN=255` and `UID=00000000` confirm no card UID was read.

Conclusion: swapping the RC522 module alone did not change the firmware-visible
failure mode. The MCU still does not receive a valid MFRC522 register response,
so the investigation should focus below the attendance business logic: BSP demo
parity, the board-to-module signal path, PB10-as-ground margin under load,
PE15/PD9/PA0/PB13/PB15 continuity and whether the RC522 MISO signal reaches the
MCU pin.

## RC522 BSP Demo And GPIO-Level Cross-Check

Date: 2026-07-02.

The BSP reference demo at `D:\c_work\BSP\Demo_RC522` was analyzed and tested.
Its RC522 path uses the same signal map as the active diagnostic firmware:

```text
GND PB10, NSS PE15, SCK PD9, MOSI PA0, MISO PB13, RST PB15
```

The demo initializes `USART1` on `PA9/PA10` at 115200 baud, calls
`RC522_Platform_Init()`, calls `RC522_ConfigISOType('A')`, and polls
`RC522_ScanCard()` every 500 ms. Although `MX_SPI1_Init()` is present, RC522
communication is software SPI in `../Bsp/NFC/rc522_platform_stm32.c`, not the
STM32 SPI1 peripheral.

The BSP prebuilt firmware was flashed directly:

```text
program D:\c_work\BSP\Demo_RC522\build\debug\NFCAttend.elf verify reset exit -> Verified OK
COM3 -> RC522 NFC Reader Demo Started
```

No `Card Detected` or UID line appeared during the observation window.

The RC522-only diagnostic was then extended with two lower-level checks:

- `DEMOLOW`: configure the active RC522 pins with BSP-demo-style low-speed GPIO,
  MOSI idle low and the same RST pulse sequence, then read `VersionReg`.
- `GPIOCHK`: drive MCU output pins low and high and read them back; `3` means
  both low and high states were observed, and `GNDL=1` means PB10 was held low.

Build/download result:

```text
make RC522_ONLY_DIAG=1 -j4 -> passed, text=109328 data=496 bss=46112
program build_rc522_only/RC522_Only_Diag.elf verify reset exit -> Verified OK
```

Representative serial output:

```text
RC522_ONLY:RAW=0xFF|VER=0xFF|CMD=0xFF->0xFF|IRQ=0xFF->0xFF|FIFO=0xFF->0xFF|TX=0xFF->0xFF|ERR=0xFF->0xFF|PINS=0x59->0x59|SHARE=0|SPD=0xFF->0xFF->0xFF|RW=0|MISO=0x03|DRVVER=00/00/00|DEMOLOW=00/00/00|GPIOCHK=NSS=3/SCK=3/MOSI=3/RST=3/GNDL=1|MAP=DEMO=00/00/00,H1=FF/FF/FF,H2=FF/FF/FF|PERM=H1=none,H2=none|REQ=255|TAG=0000|SCAN=255|UID=00000000
```

Interpretation:

- The official BSP demo starts correctly but does not report a card UID.
- `DEMOLOW=00/00/00` means the BSP-demo-style low-speed GPIO path also failed
  to read a valid MFRC522 version register.
- `GPIOCHK=NSS=3/SCK=3/MOSI=3/RST=3/GNDL=1` confirms the firmware can control
  the MCU-side output pins and hold PB10 low.
- The repeated invalid version values therefore are not explained by the
  attendance business logic, image/card protocol logic, or the STM32 SPI1
  peripheral. The remaining focus is the RC522 link itself: module orientation
  by signal name, PB10-as-ground voltage under load, continuity from PE15/PD9/
  PA0/PB13/PB15 to the module, and whether the RC522 is actually driving MISO.

## RC522 With Card Present Retest

Date: 2026-07-02.

The previous BSP demo observation was made without a card in the RC522 field.
The user then placed a card on the reader and the RC522 checks were repeated.

Current application firmware, with card present:

```text
PING -> OK:PONG
DIAG? -> DIAG:RC522_RAW=0xFF|RC522_VER=0xFF|CMD=0xFF|IRQ=0xFF|FIFO=0xFF|TX=0xFF|ERR=0xFF|PINS=0x59|SHARE=0|SPD=0xFF->0xFF->0xFF|RW=0|REQ=-1|TAG=0000
```

The BSP prebuilt RC522 demo was flashed again and observed with the card kept
on the reader:

```text
program D:\c_work\BSP\Demo_RC522\build\debug\NFCAttend.elf verify reset exit -> Verified OK
COM3 -> RC522 NFC Reader Demo Started
```

No `Card Detected` or UID line appeared during a 40 second observation window.

The enhanced RC522-only diagnostic was then rebuilt/flashed and observed with
the card present:

```text
make RC522_ONLY_DIAG=1 -j4 -> passed, text=109328 data=496 bss=46112
program build_rc522_only/RC522_Only_Diag.elf verify reset exit -> Verified OK
```

Representative diagnostic line:

```text
RC522_ONLY:RAW=0xFF|VER=0xFF|CMD=0xFF->0xFF|IRQ=0xFF->0xFF|FIFO=0xFF->0xFF|TX=0xFF->0xFF|ERR=0xFF->0xFF|PINS=0x59->0x59|SHARE=0|SPD=0xFF->0xFF->0xFF|RW=0|MISO=0x03|DRVVER=00/00/00|DEMOLOW=00/00/00|GPIOCHK=NSS=3/SCK=3/MOSI=3/RST=3/GNDL=1|MAP=DEMO=00/00/00,H1=FF/FF/FF,H2=FF/FF/FF|PERM=H1=none,H2=none|REQ=255|TAG=0000|SCAN=255|UID=00000000
```

The production application firmware was restored afterward:

```text
program build/Demo_W25Q128.elf verify reset exit -> Verified OK
PING -> OK:PONG
DIAG? -> DIAG:RC522_RAW=0xFF|RC522_VER=0xFF|CMD=0xFF|IRQ=0xFF|FIFO=0xFF|TX=0xFF|ERR=0xFF|PINS=0x59|SHARE=0|SPD=0xFF->0xFF->0xFF|RW=0|REQ=-1|TAG=0000
```

Interpretation:

- A card in the RF field did not change the RC522 register-level failure.
- Reading `VersionReg` does not require a card; it should return `0x91` or
  `0x92` from a reachable MFRC522 before `REQ/SCAN/UID` can be meaningful.
- With the card present, `REQ=255`, `SCAN=255` and `UID=00000000` confirm that
  no card response was decoded.
- The BSP demo and the enhanced diagnostic agree: the reader still does not
  provide a valid SPI response on this board connection.
