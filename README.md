# OBD2 MQTT ESP32-S3 N16R8 BLE + TPMS

Firmware này chạy trên ESP32-S3-WROOM-1-N16R8 để đọc dữ liệu OBD2 từ Vgate iCar Pro 2S BLE và đẩy lên Home Assistant qua MQTT. Bản hiện tại cũng tích hợp parser TPMS BLE kiểu DJTPMS để đọc áp suất/nhiệt độ lốp khi ESP32 ở gần xe.

## Mô Hình Hoạt Động

```text
Vgate iCar Pro 2S BLE OBD2
        |
        | BLE, IOS-Vlink
        v
ESP32-S3 N16R8
        |
        | WiFi STA, iPhone hotspot
        v
Mosquitto MQTT
        |
        v
Home Assistant

DJTPMS BLE tire sensors
        |
        | BLE advertisement scan
        v
ESP32-S3 N16R8
        |
        v
Home Assistant via MQTT
```

## Phần Cứng Đang Dùng

- Board: `ESP32-S3-WROOM-1-N16R8`
- Flash: 16 MB
- PSRAM: 8 MB
- OBD adapter: `Vgate iCar Pro 2S Bluetooth 5.2 OBD2`
- Tên BLE OBD: `IOS-Vlink`
- MAC BLE OBD: `41:42:86:9A:24:23`
- Xe: Toyota Corolla Altis 2009
- Mạng: iPhone hotspot
- MQTT broker: Mosquitto/Home Assistant
- TPMS: cảm biến BLE đọc bằng parser DJTPMS

## Cấu Hình Mặc Định

Thông tin cấu hình nằm trong [data/settings.json](data/settings.json).

README này không ghi mật khẩu WiFi/MQTT trực tiếp. Nếu cần đổi thông tin, sửa trong UI hoặc file `data/settings.json`, sau đó upload filesystem lại.

Các giá trị quan trọng:

- WiFi STA SSID: iPhone hotspot của bạn
- MQTT host: `anhkuteo.ddns.net`
- MQTT port: `8883`
- OBD name: `IOS-Vlink`
- OBD MAC: `41:42:86:9A:24:23`
- OBD protocol: `0`, tức Auto

## WiFi UI Sau Khi Flash

ESP32 tạo AP cấu hình:

- SSID: `OBD2-MQTT-<mac>`
- Password AP: `obd2mqtt`
- Địa chỉ UI: `http://192.168.4.1`

Trong code hiện tại UI không có lớp login riêng; mật khẩu cần nhập là mật khẩu WiFi AP nếu kết nối vào `OBD2-MQTT-<mac>`.

## PlatformIO Environments

Env chính trong [platformio.ini](platformio.ini):

- `ESP32S3_N16R8_BLE`: OBD BLE, không TPMS.
- `ESP32S3_N16R8_BLE_TPMS`: bản production khuyên dùng, có OBD BLE + TPMS BLE scan, log gọn.
- `ESP32S3_N16R8_BLE_TPMS_DEBUG`: bản debug, bật serial debug và log scan TPMS chi tiết.
- `ESP32S3_N16R8_BLE_TPMS_SIM`: giả lập TPMS khi không ở gần xe/cảm biến.

Bản chạy hàng ngày nên dùng:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS
```

Khi cần debug BLE/TPMS:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG
```

## Build Firmware

Build bản production:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS
```

Build bản debug:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG
```

Build TPMS simulation:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_SIM
```

## Upload Firmware

Kiểm tra cổng USB:

```bash
ls /dev/cu.usbmodem*
```

Upload production, thay port nếu máy hiện port khác:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS -t upload --upload-port /dev/cu.usbmodem101
```

Upload debug:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG -t upload --upload-port /dev/cu.usbmodem101
```

## Upload UI / Filesystem

Nếu sửa `data/settings.json`, UI, hoặc file trong thư mục `data`, cần upload filesystem:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS -t uploadfs --upload-port /dev/cu.usbmodem101
```

Nếu chỉ sửa C++ firmware thì chỉ cần `-t upload`, không cần `uploadfs`.

## Monitor Và Debug

Monitor serial production:

```bash
pio device monitor -e ESP32S3_N16R8_BLE_TPMS -p /dev/cu.usbmodem101
```

Monitor serial debug:

```bash
pio device monitor -e ESP32S3_N16R8_BLE_TPMS_DEBUG -p /dev/cu.usbmodem101
```

Thoát monitor:

```text
Ctrl+C
```

Log boot tốt thường thấy:

```text
VLinkBLEStream: found 41:42:86:9a:24:23 (IOS-Vlink)
VLinkBLEStream: connected
VLinkBLEStream: notify subscribed
Connected to ELM327
ELM327 protocol: ISO157654CAN11500
```

## Bật/Tắt Debug Log

Cơ chế log nằm trong [src/debug_log.h](src/debug_log.h).

Production:

- Không định nghĩa `ENABLE_SERIAL_DEBUG`.
- Tắt các log nặng theo chu kỳ: TPMS scan, OBD PID update, MQTT state update.
- Vẫn giữ một số log kết nối quan trọng của WiFi/MQTT/BLE/OBD.

Debug:

- Env `ESP32S3_N16R8_BLE_TPMS_DEBUG` thêm:

```ini
-DENABLE_SERIAL_DEBUG
-DTPMS_VERBOSE_SCAN
-DCORE_DEBUG_LEVEL=3
```

Dùng debug khi:

- TPMS không lên đủ 4 bánh.
- Cần xem raw manufacturer/service data BLE.
- Cần xem PID nào timeout.
- Cần xem MQTT state update chi tiết.

Sau khi ổn định, flash lại production để ESP32 chạy mát và nhẹ hơn.

## Reconnect Strategy

Firmware đã được chỉnh để phù hợp bối cảnh iPhone hotspot và Vgate/TPMS có thể không online liên tục.

WiFi:

- Dùng WiFi STA kết nối vào hotspot.
- Nếu hotspot tắt, firmware không crash.
- Khi hotspot bật lại, firmware tự reconnect.
- MQTT reconnect có backoff để tránh lặp nóng.

OBD BLE:

- Nếu Vgate mất kết nối, firmware không restart liên tục.
- OBD reconnect có backoff.
- Khi kết nối lại thành công, reset backoff.

Engine/PID cooldown:

- Nếu RPM `010C` timeout nhiều lần liên tiếp, firmware tạm dừng đọc PID động cơ trong khoảng cooldown.
- Trong cooldown vẫn ưu tiên đọc điện áp `AT RV`/battery voltage và probe từng PID nhẹ.
- Mục tiêu là tránh reconnect BLE liên tục khi xe tắt máy hoặc ECU chưa sẵn sàng.

Watchdog:

- Firmware bật task watchdog.
- Nếu task bị treo quá lâu, ESP32 sẽ reset để tự phục hồi.

## Home Assistant Sensors

Nhóm OBD chính:

- Battery Voltage
- Control Module Voltage
- Revolutions per minute
- Kilometer per Hour
- Engine Coolant Temperature
- Engine Load
- Throttle
- Mass Air Flow
- Timing Advance
- Fuel trim và các PID khác tuỳ profile trong [data/states.json](data/states.json)

Nhóm diagnostic mới:

- `OBD Last Seen`: mốc uptime giây của lần cập nhật OBD gần nhất.
- `OBD Data Age`: số giây từ lần cập nhật OBD gần nhất. Nên dùng sensor này cho automation cảnh báo dữ liệu cũ.

Nhóm CỬA XE mới (đọc từ CAN broadcast, không phải OBD request):

- `Driver Door` — cửa lái
- `Passenger Door` — cửa phụ
- `Rear Right Door` — cửa sau phải
- `Rear Left Door` — cửa sau trái

Cả 4 là `binary_sensor` với `device_class: door`, nên Home Assistant hiện
Open/Closed và dùng được thẳng trong automation. Chi tiết kỹ thuật ở mục
"Trạng Thái Cửa Xe" bên dưới.

Nhóm TPMS mới:

- `TPMS <wheel> Pressure`
- `TPMS <wheel> Pressure kPa`
- `TPMS <wheel> Temperature`
- `TPMS <wheel> Battery`
- `TPMS <wheel> Battery Voltage`
- `TPMS <wheel> RSSI`
- `TPMS <wheel> Last Seen`

Với TPMS, `Last Seen` là số giây từ lần ESP32 thấy gói BLE gần nhất của từng bánh. Nếu xe đi xa ESP32, cảm biến ngủ, hoặc bị mất sóng, giá trị này sẽ tăng dần. Đây là sensor nên dùng để tránh cảnh báo ảo.

## Cảm Biến OBD2 Của Xe

Nguyên tắc: PID nào ECU trả lời được thì publish, PID nào không thì tắt. Không
publish một con số mà firmware không tin là đúng.

### ECU hỗ trợ những PID nào

Đo ngày 2026-09-27, bật khoá và nổ máy, hỏi thẳng ECU (`7E8`, ISO 15765-4 CAN
11bit 500k) bằng các request chỉ đọc:

| Request | Trả lời | Nghĩa |
|---|---|---|
| `0100` | `4100 BE1FA813` | 01 03 04 05 06 07 0C 0D 0E 0F 10 11 13 15 1C 1F 20 |
| `0120` | `4120 9005B015` | 21 24 2E 30 31 33 34 3C 3E 40 |
| `0140` | `4140 7ADC0000` | 42 43 44 45 47 49 4A 4C 4D 4E |
| `0160` | `NO DATA` | không có PID nào từ 0x61 trở lên qua service 01 |

Odometer không nằm trong danh sách này vì nó được đọc từ CAN broadcast `0x611`,
không phải PID `0xA6`.

### Đang publish

Sức khoẻ động cơ (nên theo dõi đầu tiên):

| State | PID | Ý nghĩa |
|---|---|---|
| `milState` | 01 | Đèn check engine (on/off) |
| `numDTCs` | 01 | Số mã lỗi đã lưu; khác 0 thì mã lỗi có ở entity `DTC` |
| `monitorStatus` | 01 | Bitfield trạng thái các monitor (diagnostic) |
| `distanceWithMilOn` / `timeRunWithMilOn` | 21 / 4D | Quãng đường / thời gian chạy khi đèn check engine sáng |
| `distanceSinceCodesCleared` / `timeSinceCodesCleared` / `warmupsSinceCodesCleared` | 31 / 4E / 30 | Từ lần xoá mã lỗi gần nhất |
| `engineCoolantTemp` | 05 | Nhiệt độ nước làm mát |
| `shortTermFuelTrimBank1` / `longTermFuelTrimBank1` | 06 / 07 | Fuel trim; lệch xa 0 kéo dài là dấu hiệu hở khí nạp, kim phun, cảm biến |
| `fuelSystemStatus` | 03 | 1 open loop (máy nguội), 2 closed loop (bình thường), 4 open loop do tải, 8 open loop do lỗi, 16 closed loop có lỗi cảm biến |
| `o2SensorB1S1Lambda` | 24 | λ cảm biến oxy trước xúc tác, ~1.0 khi closed loop |
| `o2SensorB1S2Voltage` | 15 | Điện áp cảm biến oxy sau xúc tác |
| `catalystTempBank1Sensor1` / `catalystTempBank1Sensor2` | 3C / 3E | Nhiệt độ bộ xúc tác trước / sau |
| `controlModuleVoltage` / `batteryVoltage` | 42 / — | Điện áp ECU / ắc quy (13.5–14.5 V khi máy nổ là đang sạc) |

Vận hành: `rpm` (0C), `speed` (0D), `engineLoad` (04), `absoluteLoad` (43),
`throttle` (11), `relativeThrottle` (45), `throttlePositionB` (47),
`commandedThrottleActuator` (4C), `acceleratorPedalD` / `acceleratorPedalE`
(49 / 4A), `mafRate` (10), `intakeAirTemp` (0F), `timingAdvance` (0E),
`commandedEquivalenceRatio` (44), `commandedEvapPurge` (2E),
`barometricPressure` (33), `runtimeSinceEngineStart` (1F).

Thông tin tĩnh (poll 10 phút một lần): `obdStandard` (1C), `o2SensorsPresent` (13).

### Đã tắt: xe không hỗ trợ

Không có trong danh sách PID ở trên, và hỏi trực tiếp đều trả `NO DATA`:

| State | PID | Ghi chú |
|---|---|---|
| `manifoldPressure` | 0B | Máy dùng MAF, không có cảm biến MAP |
| `fuelPressure` | 0A | |
| `shortTermFuelTrimBank2` / `longTermFuelTrimBank2` | 08 / 09 | Máy 4 xy-lanh thẳng hàng chỉ có bank 1 |
| `commandedEgr` / `egrError` | 2C / 2D | |
| `fuelTankLevel` | 2F | Xem "Mức xăng: CHƯA LÀM ĐƯỢC" |
| `ambientAirTemp` | 46 | |
| `throttlePositionC` | 48 | |
| `fuelType` | 51 | |
| `engineOilTemp` | 5C | |
| `engineFuelRate` | 5E | |

Mỗi PID không hỗ trợ tốn thời gian chờ `NO DATA` trong mỗi vòng đọc, nên tắt đi
còn giúp các chỉ số thật cập nhật nhanh hơn. Entity của chúng đã được gỡ khỏi
Home Assistant bằng cách xoá discovery config retained trên broker.

### Đã tắt: đọc được nhưng firmware tính sai

`o2SensorB1S1Voltage` (PID 24, byte C–D) và `o2SensorB1S1Current` (PID 34,
byte C–D). ECU trả đúng — payload thật kiểu `41247F5968A9` — nhưng biểu thức
`$raw.b5:7` trong bản release lúc ra đúng, lúc ra 0 (tức −128 mA với current).
Bản debug và bản explorer luôn đúng, thêm một dòng log vào resolver cũng làm lỗi
biến mất: đây là undefined behavior chưa tìm ra gốc, không phải lỗi dữ liệu xe.
`o2SensorB1S1Lambda` dùng byte A–B của cùng PID vẫn luôn đúng nên được giữ lại.
`o2B1S1CurrentRaw` tắt theo vì chỉ `o2SensorB1S1Current` dùng nó.

## Tổng Kết: Đã Làm / Chưa Làm (2026-09-27)

### PID (OBD request, service 01)

| Hạng mục | Trạng thái |
|---|---|
| Danh sách PID ECU hỗ trợ (`0100`/`0120`/`0140`) | ✅ Đã đo, xem "Cảm Biến OBD2 Của Xe" |
| Mọi PID hỗ trợ đều có state | ✅ Trừ `20`/`40` (bitmap) và `13`/`1C` chỉ là thông tin tĩnh (vẫn publish, 10 phút/lần) |
| Đèn check engine, số mã lỗi (`01`) | ✅ `milState`, `numDTCs`; mã lỗi hiện ở entity `DTC` khi `numDTCs` > 0 |
| 12 PID xe không hỗ trợ | ✅ Đã tắt, entity đã gỡ khỏi HA |
| `o2SensorB1S1Voltage` / `o2SensorB1S1Current` | ❌ Tắt: firmware tính sai ở bản release (undefined behavior chưa tìm ra gốc). Không cần cho theo dõi sức khoẻ — `o2SensorB1S1Lambda` mang cùng thông tin |
| Mức xăng qua PID `2F` | ❌ Xe không hỗ trợ — phải tìm trên CAN broadcast |
| PID mở rộng của Toyota (service 21/22) | ⬜ Chưa làm |

### CAN broadcast (nghe thụ động bằng `ATMA`)

| Tín hiệu | Frame | Trạng thái |
|---|---|---|
| 4 cửa | `0x620` byte 5 bit 5/4/3/2 | ✅ Verify end-to-end (lần gần nhất 2026-09-27: cửa phụ mở → `doorPassenger` = on) |
| Odometer | `0x611` byte 5–7 | ✅ Verify, khớp đồng hồ |
| Đèn (tail/head) | `0x2C4` byte 3 bit 0 | ⚠️ **Tạm thời**, theo yêu cầu. Bit này đã bị bác bỏ trong "Đã thử và đã bị bác bỏ"; khi máy nổ byte 3 còn ra `0x2C` là giá trị chưa từng thấy lúc điều tra. Theo dõi trên HA rồi quyết định |
| Đèn cos | `0x2C1` byte 3 bit 0 | ⚠️ **Tạm thời**. Đã hết `DATA ERROR` (sửa `ATCAF0`) nên đọc được, nhưng bit chưa được kiểm chứng lại khi máy nổ |
| Mức xăng | chưa biết | 🔎 Đang theo dõi 7 byte ứng viên `fuelCandidate…` (xem "Mức xăng") |
| Khoá cửa | chưa biết | ❌ Một lần đo không ra thay đổi nào; chưa kết luận |
| Cốp, nắp ca-pô, phanh, phanh tay, xi-nhan, hazard, còi, dây an toàn, số lùi, cửa kính, gạt mưa, điều hoà, sấy kính | chưa biết | ⬜ Chưa quét. Cần người ngồi trong xe bật/tắt từng thứ trong lúc chạy `tools/toyota-explorer/live.py` |
| Tốc độ từng bánh, góc lái, phanh, ga | `0B0`/`0B2`, `2C1`/`260`/`2C4` (nghi) | ⬜ Chỉ đổi khi xe chạy — cần người thứ hai hoặc ghi log rồi phân tích sau |
| `0x620` byte 5 bit 0, bit 1; các block `621 622 630 638` | | ⬜ Chưa gán nghĩa |

Sửa firmware liên quan trong ngày: `readBroadcastFrame()` tắt CAN auto
formatting (`ATCAF0`) trong lúc nghe và bật lại sau. Frame nào có byte 0 trông
giống một PCI ISO-TP không hợp lệ (như `0x08`) trước đây bị ELM in `DATA ERROR`
thay vì dữ liệu. Sau khi sửa, cửa / odometer / các byte `0x6xx` ra đúng như cũ.

## Trạng Thái Cửa Xe

### Nguồn dữ liệu

Trạng thái cửa **không đọc được bằng PID chẩn đoán**. Xe này broadcast nó trên
CAN, frame id `0x620`, byte 5:

```text
byte5 = 0x40 khi đóng hết = 0100 0000

  bit 6  luôn = 1        (cờ khác, chưa rõ)
  bit 5  CỬA LÁI
  bit 4  CỬA PHỤ
  bit 3  CỬA SAU PHẢI
  bit 2  CỬA SAU TRÁI
  bit 1  chưa rõ
  bit 0  chưa rõ
```

bit = 1 là MỞ, bit = 0 là ĐÓNG. Bit KHÔNG xếp theo vị trí vật lý (sau trái là
bit 2, sau phải là bit 3) — số liệu đo thực tế, không suy diễn.

Cách đo và toàn bộ bằng chứng: [tools/toyota-explorer/README.md](tools/toyota-explorer/README.md).

### Firmware đọc thế nào

`OBDClass::readBroadcastFrame(canId, timeoutMs, minBytes)` hẹp bộ lọc nhận của
ELM327 về đúng một id (`ATCRA 620`), nghe `ATMA` tới khi bắt được một frame,
dừng, rồi **trả bộ lọc về** (`ATCRA` không tham số).

> ⚠️ Quên trả bộ lọc về sẽ làm chết toàn bộ PID đọc sau đó — mọi request sẽ bị
> adapter lọc mất. Bước này nằm trong hàm, đừng bỏ đi.

Hàm chỉ nhận một dòng khi dòng đó **mở đầu bằng đúng CAN id đang hỏi**, có số ký
tự chẵn, không quá 8 byte dữ liệu, và không ít hơn `minBytes`. Ba điều kiện này
đều sinh ra từ lỗi thật:

- Không bắt buộc đúng id → dòng của id khác bị đọc thành dữ liệu.
- Không chặn trần độ dài → hai frame dính nhau parse ra 10 byte, bất khả thi với
  CAN. Thủ phạm là chuỗi `DATA ERROR` của adapter: sau khi lọc bỏ ký tự không
  phải hex, nó còn lại `DAAE` và bị ghép vào frame kế tiếp.
- Không có `minBytes` → dòng bị cắt được chấp nhận, byte thiếu điền 0, và
  odometer báo 0 km.

`refreshFrame(canId, maxAgeMs, minBytes)` cache frame để không phải bắt lại mỗi
lần đọc. Khi bắt thất bại, cache bị đánh dấu không hợp lệ — **không phục vụ frame
cũ như thể còn mới**.

> ⚠️ `maxAgeMs` phải **nhỏ hơn** `interval` của state dùng nó. Nếu cache sống lâu
> hơn chu kỳ đọc thì nó không bao giờ hết hạn, và cảm biến đóng băng ở lần đọc
> thành công đầu tiên — ổn định nhưng sai, rất khó nhận ra.

### readFunc mới

Profile JSON tham chiếu các hàm này bằng tên chuỗi (`setReadFuncByName` trong
[src/obd.cpp](src/obd.cpp)):

| readFunc | valueType | Ý nghĩa |
|---|---|---|
| `bodyDoorByte` | `int` | Byte 5 nguyên bản của `0x620`, để debug. `visible: false` |
| `doorDriver` | `bool` | bit 5 |
| `doorPassenger` | `bool` | bit 4 |
| `doorRearRight` | `bool` | bit 3 |
| `doorRearLeft` | `bool` | bit 2 |
| `odometer` | `int` | `0x611` byte 5–7, km |
| `lightsOn` | `bool` | `0x2C4` byte 3 bit 0 — **tạm thời**, xem "Tổng kết" |
| `headlightsOn` | `bool` | `0x2C1` byte 3 bit 0 — **tạm thời**, xem "Tổng kết" |
| `canByte_<id>_<n>` | `int` | Byte thô thứ `n` (0–7) của frame `<id>` (hex), ví dụ `canByte_624_3`. Dùng để theo dõi byte chưa rõ nghĩa trên lịch sử HA. Cache 60s; nên đặt `retainWhenStale: true` vì frame `0x6xx` chỉ phát 20–45s một lần |

### Tại sao 4 cửa là READ state chứ không phải CALC

Cách gọn hơn là một READ state (`bodyDoorByte`) và 4 CALC state
(`$bodyDoorByte & 32`...). **Đã thử và đã bỏ.**

CALC state không có cơ chế `stale`. Khi không bắt được frame, `bodyDoorByte`
bằng 0, và cả 4 CALC tính ra "đóng" — báo SAI chứ không phải báo thiếu. Đó đúng
là bẫy "ESP32 mất kết nối → cửa đóng" cần tránh.

Nên mỗi cửa là một READ state riêng, có readFunc riêng set `nb_rx_state`.
**Đừng đổi ngược lại thành CALC.**

### Khi không có dữ liệu

`OBDState` có cờ `stale`. Khi đọc về `ELM_NO_DATA`, firmware **không gán giá trị
0 nữa** — chỉ đánh dấu `stale`, và `sendStates()` bỏ qua state đó.

Đã kiểm chứng trên xe tắt máy:

```text
OBD PID skipped: bodyDoorByte,  status 5
OBD PID skipped: doorDriver,    status 5
OBD PID skipped: doorPassenger, status 5
```

Không có giá trị cửa nào được publish. Home Assistant giữ giá trị cuối cùng
thay vì nhận một báo cáo sai.

Có một trường hợp nữa cũng là báo sai: state **chưa từng đọc thành công lần nào**
vẫn giữ giá trị khởi tạo 0. Sau mỗi lần khởi động lại hoặc mỗi lần ghi
`/api/states`, odometer sẽ công bố 0 km cho tới khi lần đọc đầu tiên về — với
chu kỳ 60 giây thì đó là một khoảng rất dài. `sendStates()` vì vậy bỏ qua luôn
state READ có `getLastUpdate() == 0`.

> Lưu ý: đây là "không cập nhật", chưa phải "unavailable" per-entity. Cả thiết bị
> chỉ có một topic availability chung (LWT). Muốn cảnh báo dữ liệu cũ, dùng sensor
> `OBD Data Age` đã có sẵn.

### Chưa làm được

**Đèn: VẪN CHƯA TÌM RA (cập nhật 2026-09-20).** Kết luận cũ — "đèn không có
trên CAN bus" — vẫn đứng cho đến khi có bằng chứng tốt hơn. Trong ngày
2026-09-20 đã ba lần tưởng là tìm ra rồi cả ba đều sai; phần dưới ghi lại để
lần sau không đi lại.

### Đã thử và đã bị bác bỏ

| Ứng viên | Vì sao trông có vẻ đúng | Vì sao sai |
| --- | --- | --- |
| `0x640` byte 5 bit 1 | 3 file capture cũ chỉ thấy `A4`/`A5` khi tắt đèn | Mẫu quá nhỏ. Chụp sạch bằng EXPLORER thì đèn tắt vẫn ra `A6`. `0x640` chỉ phát ~1 frame mỗi 30s, không dùng làm cảm biến được |
| `0x2C4` byte 3 bit 0 + bit 1 (coi là cặp bù) | 538 frame sạch khớp tuyệt đối: tắt = `26`, tail/head = `25` | Đo tiếp trên xe thì `24` xuất hiện ở nấc HEAD. `24` và `25` khác nhau đúng ở bit 0, nên bit 0 đổi ngay trong cùng một vị trí công tắc — không phải bit bù |
| `0x2C4` byte 3 bit 1 (đảo) | Khớp cả `26` (tắt), `25` và `24` (bật) | Đo lại khi TẮT đèn thì ra `25` — tức `25` xuất hiện ở **cả hai** trạng thái |
| `0x2C1` byte 3 bit 0 | Duty cycle 0.00/0.00/1.00/0.00 qua chu kỳ OFF/TAIL/HEAD/OFF | Adapter trả `DATA ERROR` cho `0x2C1` 78 lần liên tiếp, không đọc nổi một frame nào qua đường `ATCRA`+`ATMA`. **Nguyên nhân tìm ra 2026-09-27**: byte 0 của `0x2C1` là `0x08`; khi ELM bật CAN auto formatting nó hiểu đó là độ dài single frame ISO-TP = 8 (không hợp lệ) và in `DATA ERROR`. `readBroadcastFrame()` giờ tắt `ATCAF0` trong lúc `ATMA` và bật lại sau. Bit này vẫn chưa được kiểm chứng lại |

### Điều thực sự học được

Byte 1 của `0x2C4` là một giá trị **analog**, không phải cờ: đèn tắt `77-8B`,
đèn bật `BF-C8`, và nó trôi dần giữa các lần chụp (`8A` → `83` → `78`). Gần như
chắc chắn là dòng/tải điện — bật đèn làm tăng tải. Phân tích duty cycle từng bit
bắt được tương quan đó rồi quy nhầm cho các bit của byte 3.

Bài học: **538 mẫu sạch vẫn không đủ** để phân biệt "một bit cờ" với "một bit
nhiễu tình cờ đồng pha", khi tất cả đều chụp ở cùng một điều kiện. Thứ bác bỏ
được chỉ đến từ một **điều kiện khác**, không phải từ thêm mẫu.

Muốn làm tiếp thì phải đọc lại byte 1 như một đại lượng analog qua nhiều mức tải
khác nhau (đèn, điều hoà, quạt) để tách riêng dòng của đèn — hoặc đi đường ngoài
OBD:

- Đấu quang (opto-isolator) vào dây công tắc đèn, đọc bằng GPIO ESP32
- Sniff mạng BEAN bằng transceiver riêng (mạng 1 dây 10 kbps riêng của Toyota,
  nơi Body ECU thật sự nằm)

Công cụ đo đã làm trong ngày và vẫn dùng được: `tools/toyota-explorer/mon_freq.py`
(ghi **tần suất** thay vì `set()` payload như `mon_capture.py`) và `freq_diff.py`
(so duty cycle từng bit, loại nhiễu bằng cặp đối chứng, loại id ít mẫu).

Khoá cửa, cốp, nắp ca-pô: **chưa đo**. `0x620` byte 5 còn bit 0 và bit 1 chưa gán.

### Mức xăng: CHƯA LÀM ĐƯỢC

Xe **không trả lời PID `0x2F`** (Fuel Tank Level). Đã đo khi xe đang nổ máy:
`fuelTankLevel` bật, poll 15 giây một lần, không ra giá trị nào — trong khi
`engineCoolantTemp` và `intakeAirTemp` từ **chính ECU đó** vẫn publish bình
thường. Nên không phải lỗi kết nối. 11 state khác cùng cảnh: `ambientAirTemp`,
`commandedEgr`, `egrError`, `engineFuelRate`, `engineOilTemp`, `fuelPressure`,
`fuelType`, `longTermFuelTrimBank2`, `manifoldPressure`, `shortTermFuelTrimBank2`,
`throttlePositionC`.

Nhưng cụm đồng hồ **có** dữ liệu này (kim xăng hoạt động), nên nó phải nằm đâu
đó trên CAN broadcast, giống hệt cách odometer nằm trong `0x611`.

Điểm khiến bài này **dễ hơn hẳn vụ đèn**: mức xăng là đại lượng thay đổi **lớn và
một chiều**, không phải một bit bật/tắt. Từ gần `E` lên gần `F` là gần như toàn
thang — một byte đi từ ~20 lên ~200 thì không nhiễu nào giả được. Đèn thất bại
chính vì một bit đơn lẻ quá dễ trùng ngẫu nhiên với hàng trăm bit khác.

Cách làm, tận dụng lần đổ xăng bình thường chứ không cần thí nghiệm riêng:

1. Trước khi đổ (kim gần `E`), nổ máy, chụp bus 90 giây:
   `mon_freq.py fuel_low --seconds 90`
2. Đổ đầy, nổ máy, chụp lại: `mon_freq.py fuel_full --seconds 90`
3. So hai lần, lọc theo **biên độ thay đổi của byte**, không phải theo bit

⚠️ **Hai lần chụp là chưa đủ để kết luận** — đó đúng là sai lầm đã mắc ba lần với
đèn. Phải có thêm lần chụp thứ ba ở mức trung gian sau khi chạy vài ngày, và byte
đó phải **giảm dần đúng theo lượng xăng đã tiêu thụ**. Không giảm đúng chiều thì
không phải mức xăng, dù hai lần đầu khớp đẹp tới đâu.

Ứng viên đáng soi trước: `0x611` (frame chứa odometer, do cụm đồng hồ phát).
Hiện nội dung của nó là `21 00 70 10 00 01 EC 2E` — bytes 5-7 là odometer, còn
`21 00 70 10 00` chưa rõ. Trong 5 lần chụp cùng một buổi thì frame này đứng yên
hoàn toàn, đúng như kỳ vọng vì xăng gần như không đổi trong một giờ.

#### Mốc 1: kim xăng 1/4 (2026-09-27, máy nổ, odometer 126124 km)

Cặp đối chứng `freq_fuel_q1a.json` / `freq_fuel_q1b.json` (mỗi lần 90 giây,
`tools/toyota-explorer/`). Payload trội của các frame body, giống nhau ở cả hai
lần:

```text
610  20 00 00 64 00 00 00 00     624  1A 00 52 49 49 40 00 00
611  21 00 70 10 00 01 EC AC     630  17 00 00 00 00 00 00 00
620  10 00 00 00 30 40 00 50     638  13 00 1E 00 00 00 00 00
621  11 00 00 00 00 00 00 00     398  01 22
622  12 00 00 00 00 00 00 00     3B1  00 00 02 0A 00 00 00 00
```

`0x611` bytes 0–4 vẫn là `21 00 70 10 00` như lúc odometer 125864 km, tức là
đứng yên qua 260 km. Nếu trong khoảng đó xe không đổ xăng thì byte mức xăng không
nằm ở đây. Chưa xác nhận được vì không biết lịch sử đổ xăng giữa hai mốc.

Lần tiếp theo: chụp ngay sau khi đổ đầy (`mon_freq.py fuel_full_a` và
`fuel_full_b`, 90 giây mỗi lần), rồi so với mốc này.

#### Đang publish tạm để theo dõi trên Home Assistant

7 state `fuelCandidate<id>b<n>` (readFunc `canByte_<id>_<n>`, diagnostic, 60s,
`retainWhenStale`). Giá trị lúc kim 1/4:

| State | Byte | Giá trị 1/4 |
|---|---|---|
| `fuelCandidate610b3` | `0x610` byte 3 | 100 |
| `fuelCandidate611b2` | `0x611` byte 2 | 112 |
| `fuelCandidate611b3` | `0x611` byte 3 | 16 |
| `fuelCandidate624b2` | `0x624` byte 2 | 82 |
| `fuelCandidate624b3` | `0x624` byte 3 | 73 |
| `fuelCandidate624b4` | `0x624` byte 4 | 73 |
| `fuelCandidate638b2` | `0x638` byte 2 | 30 |

Byte nào là mức xăng thì phải: **nhảy mạnh ngay sau khi đổ xăng**, rồi **giảm
dần và một chiều** qua các ngày chạy. Một byte chỉ khớp một trong hai điều kiện
thì không phải. Tìm ra rồi thì thay cả 7 state bằng một state `fuelLevel` có
thang đo, và tắt các state còn lại (mỗi state tốn một lượt nghe bus).

## Gợi Ý Automation Cảnh Báo

Cảnh báo OBD mất dữ liệu:

- Điều kiện: `OBD Data Age > 120` giây.
- Chỉ cảnh báo khi MQTT device vẫn online.
- Có thể tăng ngưỡng lên `300` giây nếu iPhone hotspot không ổn định.

Cảnh báo quên đóng cửa:

- Điều kiện: bất kỳ `binary_sensor` cửa nào = `on` liên tục quá 2 phút
  VÀ `OBD Data Age` < 60 giây (tránh báo động khi dữ liệu đã cũ).
- Không báo khi xe đang đỗ và người đang xếp đồ — nên thêm điều kiện
  `Kilometer per Hour` = 0 trong dưới 5 phút, hoặc chỉ báo khi đang chạy.

Cảnh báo cửa mở lúc đang chạy:

- Điều kiện: cửa bất kỳ = `on` VÀ `Kilometer per Hour` > 5.
- Đây là cảnh báo an toàn thật, nên đặt mức ưu tiên cao hơn cảnh báo quên đóng.

Cảnh báo TPMS mất dữ liệu:

- Điều kiện: `TPMS <wheel> Last Seen > 600` giây.
- Không cảnh báo ngay sau khi xe dừng lâu vì cảm biến có thể sleep.
- Nên kết hợp với điều kiện xe đang chạy/RPM > 0 hoặc speed > 0 nếu muốn chặt hơn.

Cảnh báo áp suất lốp:

- Áp suất bình thường trong app DJTPMS bạn đang thấy khoảng `2.0-2.1 bar`.
- Nên đặt ngưỡng cảnh báo tuỳ lốp/xe, vì áp suất thay đổi theo nhiệt độ và tải trọng.

### Theo dõi sức khoẻ xe

Tất cả dưới đây là gợi ý, **chưa chạy thử trên Home Assistant**. Hai chỗ phải
thay trước khi dùng:

- **entity_id.** Entity mới tạo sinh `entity_id` từ `obj_id` của discovery, dạng
  `sensor.4142869a2423_enginecoolanttemp`. Entity cũ có thể vẫn giữ tên cũ
  (ví dụ `sensor.battery_voltage`, xem phần đổi tên entity). Tra đúng tên ở
  *Settings → Devices & services → Entities*, lọc theo "Altis".
- **`notify.notify`**: thay bằng dịch vụ của bạn, ví dụ `notify.mobile_app_<điện thoại>`.

#### 1. Cảm biến thống kê từ lịch sử (`configuration.yaml`)

Các state có `measurement: true` được recorder lưu lịch sử. Platform `statistics`
tính trên lịch sử đó:

```yaml
sensor:
  - platform: statistics
    name: "Altis LTFT 7 ngày"
    entity_id: sensor.4142869a2423_longtermfueltrimbank1
    state_characteristic: mean
    max_age: { days: 7 }
    sampling_size: 30000
  - platform: statistics
    name: "Altis nước làm mát max 24h"
    entity_id: sensor.4142869a2423_enginecoolanttemp
    state_characteristic: value_max
    max_age: { hours: 24 }
    sampling_size: 20000
  - platform: statistics
    name: "Altis ắc quy min 24h"
    entity_id: sensor.4142869a2423_batteryvoltage
    state_characteristic: value_min
    max_age: { hours: 24 }
    sampling_size: 5000
  - platform: statistics
    name: "Altis km 24h"
    entity_id: sensor.4142869a2423_odometer
    state_characteristic: change
    max_age: { hours: 24 }
    sampling_size: 5000
```

#### 2. Cảnh báo tức thời (`automations.yaml`)

```yaml
- alias: "Altis - Đèn check engine sáng"
  trigger:
    - platform: state
      entity_id: binary_sensor.4142869a2423_milstate
      to: "on"
  action:
    - service: notify.notify
      data:
        title: "⚠️ Altis: đèn check engine"
        message: >
          Đèn check engine vừa sáng. Số mã lỗi:
          {{ states('sensor.4142869a2423_numdtcs') }}.
          Mã: {{ states('sensor.4142869a2423_dtc') }}

- alias: "Altis - Có mã lỗi mới"
  trigger:
    - platform: numeric_state
      entity_id: sensor.4142869a2423_numdtcs
      above: 0
  action:
    - service: notify.notify
      data:
        title: "⚠️ Altis: có mã lỗi"
        message: "{{ trigger.to_state.state }} mã lỗi: {{ states('sensor.4142869a2423_dtc') }}"

- alias: "Altis - Nước làm mát quá nóng"
  trigger:
    - platform: numeric_state
      entity_id: sensor.4142869a2423_enginecoolanttemp
      above: 105
      for: "00:02:00"
  condition:
    - condition: state
      entity_id: binary_sensor.4142869a2423_enginerunning
      state: "on"
  action:
    - service: notify.notify
      data:
        title: "🔥 Altis: quá nhiệt"
        message: "Nước làm mát {{ trigger.to_state.state }}°C hơn 2 phút. Dừng xe kiểm tra."

- alias: "Altis - Máy phát không sạc"
  # Máy nổ mà ắc quy dưới 13.0 V kéo dài: máy phát / tiết chế có vấn đề
  trigger:
    - platform: numeric_state
      entity_id: sensor.4142869a2423_batteryvoltage
      below: 13.0
      for: "00:05:00"
  condition:
    - condition: state
      entity_id: binary_sensor.4142869a2423_enginerunning
      state: "on"
  action:
    - service: notify.notify
      data:
        title: "🔋 Altis: không sạc"
        message: "Máy nổ nhưng ắc quy chỉ {{ trigger.to_state.state }} V trong 5 phút."

- alias: "Altis - Fuel trim lệch kéo dài"
  # Chỉ tin fuel trim khi đang closed loop (fuelSystemStatus = 2)
  trigger:
    - platform: template
      value_template: >
        {{ states('sensor.4142869a2423_fuelsystemstatus') | int(0) == 2 and
           (states('sensor.4142869a2423_longtermfueltrimbank1') | float(0)) | abs > 10 }}
      for: "00:10:00"
  action:
    - service: notify.notify
      data:
        title: "⛽ Altis: fuel trim lệch"
        message: >
          LTFT {{ states('sensor.4142869a2423_longtermfueltrimbank1') }}% hơn 10 phút.
          Dương = hỗn hợp nghèo (hở khí nạp, MAF bẩn, bơm xăng yếu);
          âm = hỗn hợp giàu (kim phun rò, cảm biến oxy).

- alias: "Altis - Máy không đạt nhiệt độ làm việc"
  # Chạy 20 phút mà nước vẫn dưới 75°C: nghi van hằng nhiệt kẹt mở
  trigger:
    - platform: numeric_state
      entity_id: sensor.4142869a2423_runtimesinceenginestart
      above: 1200
  condition:
    - condition: numeric_state
      entity_id: sensor.4142869a2423_enginecoolanttemp
      below: 75
  action:
    - service: notify.notify
      data:
        title: "🌡️ Altis: máy nguội bất thường"
        message: "Đã chạy 20 phút, nước làm mát chỉ {{ states('sensor.4142869a2423_enginecoolanttemp') }}°C."

- alias: "Altis - Xu hướng LTFT 7 ngày"
  # Dựa trên lịch sử: trung bình 7 ngày trôi quá ±7% là hỏng dần, chưa đủ bật đèn
  trigger:
    - platform: numeric_state
      entity_id: sensor.altis_ltft_7_ngay
      above: 7
    - platform: numeric_state
      entity_id: sensor.altis_ltft_7_ngay
      below: -7
  action:
    - service: notify.notify
      data:
        title: "📈 Altis: fuel trim trôi dần"
        message: "Trung bình LTFT 7 ngày: {{ trigger.to_state.state }}%. Nên đi kiểm tra trước khi thành mã lỗi."
```

#### 3. Báo cáo sức khoẻ hằng ngày

```yaml
- alias: "Altis - Báo cáo sức khoẻ hằng ngày"
  trigger:
    - platform: time
      at: "20:00:00"
  action:
    - service: notify.notify
      data:
        title: "🚗 Altis - sức khoẻ ngày {{ now().strftime('%d/%m') }}"
        message: >
          {% set mil = states('binary_sensor.4142869a2423_milstate') %}
          {% set dtc = states('sensor.4142869a2423_numdtcs') | int(0) %}
          {% set ltft = states('sensor.altis_ltft_7_ngay') | float(0) %}
          {% set cool = states('sensor.altis_nuoc_lam_mat_max_24h') | float(0) %}
          {% set bat = states('sensor.altis_ac_quy_min_24h') | float(0) %}
          {% set age = states('sensor.4142869a2423_obddataage') | int(-1) %}
          Check engine: {{ '🔴 SÁNG' if mil == 'on' else '🟢 tắt' }}
          Mã lỗi: {{ '🟢 0' if dtc == 0 else '🔴 ' ~ dtc ~ ' (' ~ states('sensor.4142869a2423_dtc') ~ ')' }}
          Nước làm mát max 24h: {{ cool }}°C {{ '🔴' if cool > 105 else '🟢' }}
          Ắc quy min 24h: {{ bat }} V {{ '🔴' if bat < 11.8 else ('🟡' if bat < 12.2 else '🟢') }}
          LTFT TB 7 ngày: {{ ltft | round(1) }}% {{ '🔴' if ltft | abs > 10 else ('🟡' if ltft | abs > 5 else '🟢') }}
          Quãng đường 24h: {{ states('sensor.altis_km_24h') }} km
          Odometer: {{ states('sensor.4142869a2423_odometer') }} km
          Km từ lần xoá lỗi: {{ states('sensor.4142869a2423_distancesincecodescleared') }} km
          {% if age < 0 or age > 3600 %}⚠️ Dữ liệu OBD cũ hơn 1 giờ - số liệu trên có thể là của hôm trước.{% endif %}
```

Ngưỡng màu dựa trên giá trị tham khảo chung cho xe xăng, không phải thông số
riêng của Corolla Altis: ắc quy nghỉ ≥ 12.4 V là đầy, dưới 12.2 V là yếu; LTFT
trong ±5% là bình thường, ngoài ±10% là có vấn đề.

## TPMS DJTPMS

Parser hiện tại đọc gói BLE manufacturer/service data theo mẫu DJTPMS đã test từ nRF Connect.

Mapping cảm biến:

| Vị trí | MAC |
| --- | --- |
| Front left | `d0:0c:5e:5c:62:96` |
| Front right | `d0:0c:5e:5c:7d:bf` |
| Rear left | `54:6c:50:63:ae:87` |
| Rear right | `d0:0c:5e:5c:5a:c6` |

Chú ý:

- `battery_voltage` của DJTPMS hiện có thể là `0.00 V` vì byte pin trong payload đang giống status/percent hơn là điện áp thật.
- `battery` đang map về percent/status để HA hiển thị dễ đọc hơn.
- Nếu thấy vị trí bánh sai, sửa mapping MAC trong [src/tpms.cpp](src/tpms.cpp).

## Kịch Bản Test

Test WiFi hotspot:

1. Tắt hotspot iPhone.
2. Mở monitor.
3. Xác nhận firmware không crash, chỉ báo WiFi/MQTT fail/retry.
4. Bật lại hotspot.
5. Xác nhận log có WiFi recovered/MQTT reconnect.
6. Kiểm tra HA device online lại.

Test OBD khi xe tắt máy:

1. Cắm Vgate vào cổng OBD.
2. Bật khoá ON, chưa nổ máy.
3. Xác nhận ESP32 kết nối `IOS-Vlink`.
4. Điện áp nên có giá trị khoảng 11-12V.
5. RPM có thể bằng 0 hoặc timeout; firmware sẽ vào cooldown nếu ECU chưa trả lời.

Test OBD khi nổ máy:

1. Nổ máy.
2. Chờ 30-60 giây.
3. Kiểm tra HA các giá trị RPM, coolant, throttle, MAF, speed.
4. Nếu `OBD Data Age` không tăng quá cao và giá trị PID cập nhật, OBD OK.

Test TPMS:

1. Đặt ESP32 gần xe.
2. Dùng env debug nếu cần soi gói BLE:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG -t upload --upload-port /dev/cu.usbmodem101
pio device monitor -e ESP32S3_N16R8_BLE_TPMS_DEBUG -p /dev/cu.usbmodem101
```

3. Chờ tối thiểu 1-2 chu kỳ scan.
4. Trên HA kiểm tra 4 bánh có pressure/temperature/RSSI/last_seen.
5. Sau khi OK, flash lại production:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS -t upload --upload-port /dev/cu.usbmodem101
```

## Lệnh Kiểm Tra Mạng Trên Mac

Kiểm tra laptop đang kết nối WiFi nào:

```bash
networksetup -getairportnetwork en0
```

Danh sách WiFi đã lưu:

```bash
networksetup -listpreferredwirelessnetworks en0
```

Kiểm tra broker MQTT TLS/TCP:

```bash
openssl s_client -connect anhkuteo.ddns.net:8883 -servername anhkuteo.ddns.net -brief
```

Kiểm tra port USB:

```bash
ls /dev/cu.usbmodem*
```

## Lỗi Thường Gặp

Tất cả giá trị HA bằng 0:

- Kiểm tra ESP32 có kết nối MQTT không.
- Kiểm tra HA đã nhận discovery mới chưa.
- Kiểm tra Vgate có kết nối BLE không.
- Nếu xe chưa nổ máy, nhiều PID động cơ có thể bằng 0 hoặc timeout.
- Xem `OBD Data Age`; nếu tăng cao thì dữ liệu OBD đang cũ.

Một giá trị trong HA đứng yên mãi không đổi:

- Kiểm tra topic của nó có discovery config không. Topic retained còn sót lại từ
  profile `states.json` cũ sẽ không ai publish nữa nên đứng yên vĩnh viễn, trong
  khi entity vẫn hiển thị như bình thường.
- So danh sách topic retained `obd2mqtt/#` với `uniq_id` trong `homeassistant/#`;
  cái nào có topic mà không có config là mồ côi. Xoá bằng cách publish payload
  rỗng có retain, rồi xoá entity trong HA.

Không thấy TPMS:

- Cảm biến TPMS có thể sleep.
- Di chuyển xe nhẹ hoặc chờ cảm biến phát BLE lại.
- Dùng env debug để xem có thấy MAC/payload không.
- Kiểm tra mapping MAC trong `src/tpms.cpp`.

WiFi iPhone không reconnect:

- Kiểm tra đúng SSID hotspot.
- iPhone hotspot có thể tự tắt khi không có client; bật lại hotspot và chờ ESP32 retry.
- Dùng monitor xem log WiFi/MQTT reconnect.

BLE Vgate không connect:

- Kiểm tra Vgate đã cắm vào cổng OBD và xe có cấp nguồn.
- Name phải là `IOS-Vlink`.
- MAC phải là `41:42:86:9A:24:23`.
- Firmware đang dùng profile NimBLE VLink `E781/BEF8` cho iOS Vlink.

## Backup

Backup trước khi thêm TPMS nằm ở:

```text
backups/pre-tpms-20260822-074504
```

Backup này gồm patch thay đổi và các file untracked tại thời điểm trước TPMS.
