# OBD2 MQTT ESP32-S3 N16R8 BLE + TPMS

Firmware nay chay tren ESP32-S3-WROOM-1-N16R8 de doc du lieu OBD2 tu Vgate iCar Pro 2S BLE va day len Home Assistant qua MQTT. Ban hien tai cung tich hop parser TPMS BLE kieu DJTPMS de doc ap suat/nhiet do lop khi ESP32 o gan xe.

## Mo Hinh Hoat Dong

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

## Phan Cung Dang Dung

- Board: `ESP32-S3-WROOM-1-N16R8`
- Flash: 16 MB
- PSRAM: 8 MB
- OBD adapter: `Vgate iCar Pro 2S Bluetooth 5.2 OBD2`
- Ten BLE OBD: `IOS-Vlink`
- MAC BLE OBD: `41:42:86:9A:24:23`
- Xe: Toyota Corolla Altis 2009
- Mang: iPhone hotspot
- MQTT broker: Mosquitto/Home Assistant
- TPMS: cam bien BLE doc bang parser DJTPMS

## Cau Hinh Mac Dinh

Thong tin cau hinh nam trong [data/settings.json](/Users/huuthanh3108/app/obd2-mqtt/data/settings.json).

README nay khong ghi mat khau WiFi/MQTT truc tiep. Neu can doi thong tin, sua trong UI hoac file `data/settings.json`, sau do upload filesystem lai.

Cac gia tri quan trong:

- WiFi STA SSID: iPhone hotspot cua ban
- MQTT host: `anhkuteo.ddns.net`
- MQTT port: `8883`
- OBD name: `IOS-Vlink`
- OBD MAC: `41:42:86:9A:24:23`
- OBD protocol: `0`, tuc Auto

## WiFi UI Sau Khi Flash

ESP32 tao AP cau hinh:

- SSID: `OBD2-MQTT-<mac>`
- Password AP: `obd2mqtt`
- Dia chi UI: `http://192.168.4.1`

Trong code hien tai UI khong co lop login rieng; mat khau can nhap la mat khau WiFi AP neu ket noi vao `OBD2-MQTT-<mac>`.

## PlatformIO Environments

Env chinh trong [platformio.ini](/Users/huuthanh3108/app/obd2-mqtt/platformio.ini):

- `ESP32S3_N16R8_BLE`: OBD BLE, khong TPMS.
- `ESP32S3_N16R8_BLE_TPMS`: ban production khuyen dung, co OBD BLE + TPMS BLE scan, log gon.
- `ESP32S3_N16R8_BLE_TPMS_DEBUG`: ban debug, bat serial debug va log scan TPMS chi tiet.
- `ESP32S3_N16R8_BLE_TPMS_SIM`: gia lap TPMS khi khong o gan xe/cam bien.

Ban chay hang ngay nen dung:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS
```

Khi can debug BLE/TPMS:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG
```

## Build Firmware

Build ban production:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS
```

Build ban debug:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG
```

Build TPMS simulation:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_SIM
```

## Upload Firmware

Kiem tra cong USB:

```bash
ls /dev/cu.usbmodem*
```

Upload production, thay port neu may hien port khac:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS -t upload --upload-port /dev/cu.usbmodem101
```

Upload debug:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG -t upload --upload-port /dev/cu.usbmodem101
```

## Upload UI / Filesystem

Neu sua `data/settings.json`, UI, hoac file trong thu muc `data`, can upload filesystem:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS -t uploadfs --upload-port /dev/cu.usbmodem101
```

Neu chi sua C++ firmware thi chi can `-t upload`, khong can `uploadfs`.

## Monitor Va Debug

Monitor serial production:

```bash
pio device monitor -e ESP32S3_N16R8_BLE_TPMS -p /dev/cu.usbmodem101
```

Monitor serial debug:

```bash
pio device monitor -e ESP32S3_N16R8_BLE_TPMS_DEBUG -p /dev/cu.usbmodem101
```

Thoat monitor:

```text
Ctrl+C
```

Log boot tot thuong thay:

```text
VLinkBLEStream: found 41:42:86:9a:24:23 (IOS-Vlink)
VLinkBLEStream: connected
VLinkBLEStream: notify subscribed
Connected to ELM327
ELM327 protocol: ISO157654CAN11500
```

## Bat/Tat Debug Log

Co che log nam trong [src/debug_log.h](/Users/huuthanh3108/app/obd2-mqtt/src/debug_log.h).

Production:

- Khong dinh nghia `ENABLE_SERIAL_DEBUG`.
- Tat cac log nang theo chu ky: TPMS scan, OBD PID update, MQTT state update.
- Van giu mot so log ket noi quan trong cua WiFi/MQTT/BLE/OBD.

Debug:

- Env `ESP32S3_N16R8_BLE_TPMS_DEBUG` them:

```ini
-DENABLE_SERIAL_DEBUG
-DTPMS_VERBOSE_SCAN
-DCORE_DEBUG_LEVEL=3
```

Dung debug khi:

- TPMS khong len du 4 banh.
- Can xem raw manufacturer/service data BLE.
- Can xem PID nao timeout.
- Can xem MQTT state update chi tiet.

Sau khi on dinh, flash lai production de ESP32 chay mat va nhe hon.

## Reconnect Strategy

Firmware da duoc chinh de phu hop boi canh iPhone hotspot va Vgate/TPMS co the khong online lien tuc.

WiFi:

- Dung WiFi STA ket noi vao hotspot.
- Neu hotspot tat, firmware khong crash.
- Khi hotspot bat lai, firmware tu reconnect.
- MQTT reconnect co backoff de tranh lap nong.

OBD BLE:

- Neu Vgate mat ket noi, firmware khong restart lien tuc.
- OBD reconnect co backoff.
- Khi ket noi lai thanh cong, reset backoff.

Engine/PID cooldown:

- Neu RPM `010C` timeout nhieu lan lien tiep, firmware tam dung doc PID dong co trong khoang cooldown.
- Trong cooldown van uu tien doc dien ap `AT RV`/battery voltage va probe tung PID nhe.
- Muc tieu la tranh reconnect BLE lien tuc khi xe tat may hoac ECU chua san sang.

Watchdog:

- Firmware bat task watchdog.
- Neu task bi treo qua lau, ESP32 se reset de tu phuc hoi.

## Home Assistant Sensors

Nhom OBD chinh:

- Battery Voltage
- Control Module Voltage
- Revolutions per minute
- Kilometer per Hour
- Engine Coolant Temperature
- Engine Load
- Throttle
- Mass Air Flow
- Timing Advance
- Fuel trim va cac PID khac tuy profile trong [data/states.json](/Users/huuthanh3108/app/obd2-mqtt/data/states.json)

Nhom diagnostic moi:

- `OBD Last Seen`: moc uptime giay cua lan cap nhat OBD gan nhat.
- `OBD Data Age`: so giay tu lan cap nhat OBD gan nhat. Nen dung sensor nay cho automation canh bao du lieu cu.

Nhom TPMS moi:

- `TPMS <wheel> Pressure`
- `TPMS <wheel> Pressure kPa`
- `TPMS <wheel> Temperature`
- `TPMS <wheel> Battery`
- `TPMS <wheel> Battery Voltage`
- `TPMS <wheel> RSSI`
- `TPMS <wheel> Last Seen`

Voi TPMS, `Last Seen` la so giay tu lan ESP32 thay goi BLE gan nhat cua tung banh. Neu xe di xa ESP32, cam bien ngu, hoac bi mat song, gia tri nay se tang dan. Day la sensor nen dung de tranh canh bao ao.

## Goi Y Automation Canh Bao

Canh bao OBD mat du lieu:

- Dieu kien: `OBD Data Age > 120` giay.
- Chi canh bao khi MQTT device van online.
- Co the tang nguong len `300` giay neu iPhone hotspot khong on dinh.

Canh bao TPMS mat du lieu:

- Dieu kien: `TPMS <wheel> Last Seen > 600` giay.
- Khong canh bao ngay sau khi xe dung lau vi cam bien co the sleep.
- Nen ket hop voi dieu kien xe dang chay/RPM > 0 hoac speed > 0 neu muon chat hon.

Canh bao ap suat lop:

- Ap suat binh thuong trong app DJTPMS ban dang thay khoang `2.0-2.1 bar`.
- Nen dat nguong canh bao tuy lop/xe, vi ap suat thay doi theo nhiet do va tai trong.

## TPMS DJTPMS

Parser hien tai doc goi BLE manufacturer/service data theo mau DJTPMS da test tu nRF Connect.

Mapping cam bien:

| Vi tri | MAC |
| --- | --- |
| Front left | `d0:0c:5e:5c:62:96` |
| Front right | `d0:0c:5e:5c:7d:bf` |
| Rear left | `54:6c:50:63:ae:87` |
| Rear right | `d0:0c:5e:5c:5a:c6` |

Chu y:

- `battery_voltage` cua DJTPMS hien co the la `0.00 V` vi byte pin trong payload dang giong status/percent hon la dien ap that.
- `battery` dang map ve percent/status de HA hien thi de doc hon.
- Neu thay vi tri banh sai, sua mapping MAC trong [src/tpms.cpp](/Users/huuthanh3108/app/obd2-mqtt/src/tpms.cpp).

## Kich Ban Test

Test WiFi hotspot:

1. Tat hotspot iPhone.
2. Mo monitor.
3. Xac nhan firmware khong crash, chi bao WiFi/MQTT fail/retry.
4. Bat lai hotspot.
5. Xac nhan log co WiFi recovered/MQTT reconnect.
6. Kiem tra HA device online lai.

Test OBD khi xe tat may:

1. Cam Vgate vao cong OBD.
2. Bat khoa ON, chua no may.
3. Xac nhan ESP32 ket noi `IOS-Vlink`.
4. Dien ap nen co gia tri khoang 11-12V.
5. RPM co the bang 0 hoac timeout; firmware se vao cooldown neu ECU chua tra loi.

Test OBD khi no may:

1. No may.
2. Cho 30-60 giay.
3. Kiem tra HA cac gia tri RPM, coolant, throttle, MAF, speed.
4. Neu `OBD Data Age` khong tang qua cao va gia tri PID cap nhat, OBD OK.

Test TPMS:

1. Dat ESP32 gan xe.
2. Dung env debug neu can soi goi BLE:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS_DEBUG -t upload --upload-port /dev/cu.usbmodem101
pio device monitor -e ESP32S3_N16R8_BLE_TPMS_DEBUG -p /dev/cu.usbmodem101
```

3. Cho toi thieu 1-2 chu ky scan.
4. Tren HA kiem tra 4 banh co pressure/temperature/RSSI/last_seen.
5. Sau khi OK, flash lai production:

```bash
pio run -e ESP32S3_N16R8_BLE_TPMS -t upload --upload-port /dev/cu.usbmodem101
```

## Lenh Kiem Tra Mang Tren Mac

Kiem tra laptop dang ket noi WiFi nao:

```bash
networksetup -getairportnetwork en0
```

Danh sach WiFi da luu:

```bash
networksetup -listpreferredwirelessnetworks en0
```

Kiem tra broker MQTT TLS/TCP:

```bash
openssl s_client -connect anhkuteo.ddns.net:8883 -servername anhkuteo.ddns.net -brief
```

Kiem tra port USB:

```bash
ls /dev/cu.usbmodem*
```

## Loi Thuong Gap

Tat ca gia tri HA bang 0:

- Kiem tra ESP32 co ket noi MQTT khong.
- Kiem tra HA da nhan discovery moi chua.
- Kiem tra Vgate co ket noi BLE khong.
- Neu xe chua no may, nhieu PID dong co co the bang 0 hoac timeout.
- Xem `OBD Data Age`; neu tang cao thi du lieu OBD dang cu.

Khong thay TPMS:

- Cam bien TPMS co the sleep.
- Di chuyen xe nhe hoac cho cam bien phat BLE lai.
- Dung env debug de xem co thay MAC/payload khong.
- Kiem tra mapping MAC trong `src/tpms.cpp`.

WiFi iPhone khong reconnect:

- Kiem tra dung SSID hotspot.
- iPhone hotspot co the tu tat khi khong co client; bat lai hotspot va cho ESP32 retry.
- Dung monitor xem log WiFi/MQTT reconnect.

BLE Vgate khong connect:

- Kiem tra Vgate da cam vao cong OBD va xe co cap nguon.
- Name phai la `IOS-Vlink`.
- MAC phai la `41:42:86:9A:24:23`.
- Firmware dang dung profile NimBLE VLink `E781/BEF8` cho iOS Vlink.

## Backup

Backup truoc khi them TPMS nam o:

```text
/Users/huuthanh3108/app/obd2-mqtt/backups/pre-tpms-20260822-074504
```

Backup nay gom patch thay doi va cac file untracked tai thoi diem truoc TPMS.
