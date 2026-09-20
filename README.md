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
| `0x2C1` byte 3 bit 0 | Duty cycle 0.00/0.00/1.00/0.00 qua chu kỳ OFF/TAIL/HEAD/OFF | Adapter trả `DATA ERROR` cho `0x2C1` 78 lần liên tiếp, không đọc nổi một frame nào qua đường `ATCRA`+`ATMA` |

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
