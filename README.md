# ID100 Battery Monitor

Firmware giám sát điện áp pin ID100 bằng ESP32-C3, hỗ trợ 4 kênh ADC, cấu hình
qua Web AP và trao đổi dữ liệu với server bằng MQTT qua TLS.

Hệ thống thực hiện hai nhiệm vụ chính:

- Đo và hiển thị tức thời 4 kênh trên Web AP cục bộ.
- Khi server cho một kênh chạy test, lấy mẫu mỗi giây, tính trung bình 10 phút
  và gửi telemetry lên MQTT với QoS 1.

Nếu mất Wi-Fi hoặc MQTT, các kênh đang `running` vẫn tiếp tục đo. Telemetry hoàn
chỉnh chưa gửi được được lưu trong LittleFS và gửi bù khi kết nối trở lại.

## Phần cứng

| Kênh | Chân ADC | Tên mặc định |
|---|---:|---|
| CH1 | GPIO1 | ID-100 Unit 01 |
| CH2 | GPIO2 | ID-100 Unit 02 |
| CH3 | GPIO3 | ID-100 Unit 03 |
| CH4 | GPIO4 | ID-100 Unit 04 |

Đầu vào phải đi qua mạch chia áp phù hợp và dùng chung GND với ESP32-C3. Không
đưa điện áp vượt giới hạn ADC trực tiếp vào GPIO.

## Cách đo pin

### Đọc ADC

Mỗi lần đo một kênh, firmware thực hiện 32 lần `analogRead()` và
`analogReadMilliVolts()`, cách nhau 150 micro giây, rồi lấy trung bình. ADC dùng
độ phân giải 12 bit và attenuation `ADC_11db`.

```cpp
#define ADC_SAMPLES 32
#define ADC_SAMPLE_DELAY_US 150
#define ADC_SAMPLE_INTERVAL_MS 1000UL
```

### Quy đổi điện áp

Với mạch điện trở 5,6 MΩ và 1,1 MΩ, điện áp pin được hiệu chuẩn theo:

```text
battery_voltage_mv = adc_voltage_mv × 6.11788007744669
                     + 26.958024845082
```

Phần trăm pin được nội suy tuyến tính từ 2.400 mV (0%) đến 3.300 mV (100%) và
giới hạn trong khoảng 0–100%.

| Điện áp pin | Trạng thái Web AP |
|---:|---|
| Dưới 500 mV | `not_connected` |
| 500–2.400 mV | `empty` |
| Trên 2.400 và dưới 2.500 mV | `low` |
| 2.500 đến dưới 3.300 mV | `normal` |
| 3.300 đến 3.450 mV | `full` |
| Trên 3.450 mV | `over_voltage` |

`battery_status` không được gửi trong telemetry; server tự đánh giá pin từ dữ
liệu điện áp hoặc phần trăm.

## Lấy mẫu và trung bình 10 phút

Mỗi kênh có trạng thái độc lập: `idle`, `running` hoặc `paused`.

Khi kênh `running`:

1. Mỗi giây tạo một mẫu đã trung bình từ 32 lần đọc ADC.
2. Cộng dồn 60 mẫu để tạo trung bình một phút.
3. Cộng dồn 10 kết quả một phút để tạo trung bình 10 phút.
4. Ghi telemetry vào LittleFS trước khi thử gửi MQTT.
5. Chỉ xóa file sau khi broker trả PUBACK.

Firmware chỉ giữ tổng và bộ đếm trong RAM, không lưu mảng 600 mẫu. Lượng RAM cho
phép tính trung bình vì vậy không tăng theo thời gian.

Ngay sau lệnh `start`, firmware đo và tạo telemetry đầu tiên với chu kỳ tổng hợp
1 giây. Mẫu đó vẫn được cộng vào phút đầu tiên.

Khi `idle` hoặc `paused`, ADC vẫn được đọc mỗi giây để Web AP hiển thị tức thời,
nhưng không cộng vào trung bình và không gửi server. `pause` giữ phần tổng đang
tính; `resume` tiếp tục lấy đủ số mẫu còn thiếu, không tính thời gian bị pause.
`stop` xóa phần trung bình chưa hoàn thành trong RAM.

## Nhận dạng thiết bị

`gateway_id` được tạo ổn định từ MAC Wi-Fi STA:

```text
GW-{12 ký tự MAC viết hoa}
```

Ví dụ:

```text
Gateway: GW-80B54E1FDF14
CH1:     GW-80B54E1FDF14-CH1
CH2:     GW-80B54E1FDF14-CH2
CH3:     GW-80B54E1FDF14-CH3
CH4:     GW-80B54E1FDF14-CH4
```

`boot_id` là UUID mới sau mỗi lần khởi động. Trạng thái, `session_id` và
`sequence_no` của từng kênh được lưu trong NVS và được khôi phục sau reset/mất
nguồn. Sequence tăng độc lập trên từng kênh nên CH1–CH4 không bắt buộc giống nhau.

## Web AP

ESP luôn phát mạng cấu hình:

```text
SSID:     ID100-Battery-Monitor
Password: 12345678
Web:      http://4.4.4.4
```

AP chạy cùng kết nối router bằng `WIFI_AP_STA`. Wi-Fi sleep bị tắt; firmware kiểm
tra AP mỗi 5 giây và khởi động lại AP nếu cần.

Web AP cho phép:

- Nhập `Gateway name`, `Tester name`, `Test location`.
- Quét/chọn Wi-Fi router và nhập mật khẩu.
- Xem SSID đang kết nối, IP và RSSI.
- Xem MQTT, trạng thái kênh, ADC raw, ADC mV, điện áp và phần trăm pin.

Cả ba trường Test Information đều bắt buộc trước khi `start`. Thông tin test và
Wi-Fi được lưu trong NVS. Khi có điện thoại đang dùng AP, firmware không tự thử
lại router để ưu tiên độ ổn định AP; khi không có client AP, STA thử lại Wi-Fi đã
lưu sau mỗi 60 giây.

## MQTT

| Thuộc tính | Giá trị |
|---|---|
| Protocol | MQTT 3.1.1 |
| Transport | TLS, port 8883 |
| Client ID | `gateway_id` |
| QoS | 1 |
| Keep Alive | 30 giây |
| Clean session | Tắt |
| Reconnect | Firmware tự quản lý, 1–30 giây |

Thông tin broker, username, password và CA certificate nằm trong `src/config.h`.
Không nên đưa credential production lên repository public; nên chuyển chúng sang
secret build-time hoặc file cấu hình không được Git theo dõi trước khi công khai.

### Topics

| Chức năng | Topic |
|---|---|
| Gateway status | `id100/{gateway_id}/status` |
| Telemetry | `id100/{gateway_id}/telemetry` |
| Server command | `id100/{gateway_id}/+/desired` |
| Command ACK | `id100/{gateway_id}/{device_id}/ack` |

### Luồng kết nối

1. ESP kết nối Wi-Fi router và đồng bộ NTP.
2. MQTT kết nối TLS và subscribe `desired` với QoS 1.
3. Sau SUBACK, ESP publish `online`, QoS 1, retain `true`.
4. ESP đo và publish bootstrap telemetry gồm đủ CH1–CH4, kể cả kênh idle.
5. Sau PUBACK bootstrap, ESP mới gửi lần lượt telemetry đang chờ.

Bootstrap giúp server nhận diện gateway và toàn bộ 4 kênh sau khi khởi động. Nó
chỉ được tạo khi Test Information đã đầy đủ.

Payload online:

```json
{
  "schema_version": 1,
  "type": "gateway_status",
  "gateway_id": "GW-80B54E1FDF14",
  "status": "online",
  "gateway_name": "Skylink test 01",
  "firmware_version": "0.1.0-demo",
  "boot_id": "uuid-cua-lan-khoi-dong"
}
```

## Last Will

Last Will được cấu hình trước khi connect:

- Topic `id100/{gateway_id}/status`.
- QoS 1, retain `true`.
- Keep Alive 30 giây.

```json
{
  "schema_version": 1,
  "type": "gateway_status",
  "gateway_id": "GW-80B54E1FDF14",
  "status": "offline",
  "reason": "connection_lost"
}
```

Nếu ESP mất nguồn/mạng mà không disconnect đúng quy trình, broker phát Last Will.
Thời gian nhận offline phụ thuộc broker và mạng, không nhất thiết chính xác đúng
30 giây. Khi kết nối lại, online retained ghi đè offline retained.

## Command và ACK

| Action | desired_state | Điều kiện |
|---|---|---|
| `start` | `running` | Kênh idle và Test Information đầy đủ |
| `pause` | `paused` | Kênh running |
| `resume` | `running` | Kênh paused và đúng `session_id` |
| `stop` | `stopped` | Chuyển về idle |

Ví dụ start CH1:

```json
{
  "schema_version": 1,
  "type": "test_command",
  "command_id": "test-start-ch1-001",
  "gateway_id": "GW-80B54E1FDF14",
  "device_id": "GW-80B54E1FDF14-CH1",
  "channel": 1,
  "action": "start",
  "desired_state": "running",
  "session_id": "session-001"
}
```

ACK thành công có `result: "ok"`. ACK lỗi có `result: "error"`, `error_code`
và `error_message`. Các lỗi gồm `INVALID_COMMAND_ID`, `INVALID_TYPE`,
`INVALID_GATEWAY`, `INVALID_DEVICE`, `CONFIG_REQUIRED`, `INVALID_STATE`,
`INVALID_SESSION` và `INVALID_ACTION`.

Firmware cache 10 ACK gần nhất. Nếu server gửi lại cùng `command_id`, ESP trả ACK
đã cache và không thực hiện action lần thứ hai.

## Telemetry payload

```json
{
  "schema_version": 1,
  "type": "telemetry_batch",
  "gateway_id": "GW-80B54E1FDF14",
  "gateway_name": "Skylink test 01",
  "firmware_version": "0.1.0-demo",
  "boot_id": "uuid-cua-lan-khoi-dong",
  "tester_name": "Skylink_Tomn",
  "test_location": "ID-100 Test Lab",
  "measurements": [
    {
      "device_id": "GW-80B54E1FDF14-CH1",
      "channel": 1,
      "sequence_no": 25,
      "captured_at": "2026-09-05T04:10:00.000Z",
      "adc_raw": 700,
      "adc_voltage_mv": 513,
      "battery_voltage_mv": 3165,
      "battery_percent": 85.0,
      "wifi_rssi": -58,
      "uptime_s": 650,
      "is_backfill": false
    }
  ]
}
```

Một batch có thể chứa một hoặc nhiều measurement. Payload không có `label` và
không có `battery_status`.

## Offline buffer và backfill

Mỗi telemetry được ghi LittleFS trước khi publish:

```text
Tạo telemetry → ghi LittleFS → publish QoS 1 → PUBACK → xóa file
```

Khi mất mạng, file vẫn còn và measurement được đánh dấu `is_backfill: true`.
Sau reconnect, ESP gửi từ cũ tới mới. Hàng đợi chỉ được đẩy khi có ít nhất một
kênh `running`.

Trong 3 giờ đầu offline, firmware giữ bản ghi 10 phút. Từ 3 giờ trở đi, mỗi 6 bản
ghi 10 phút có cùng tập kênh được gộp thành một bản trung bình 1 giờ. Việc kiểm
tra nén diễn ra mỗi 60 giây. Metadata nén chỉ dùng nội bộ và bị xóa trước khi gửi,
do đó schema server không thay đổi.

Hàng đợi giới hạn 200 file; vượt giới hạn sẽ xóa file cũ nhất. Lệnh `stop` xóa
measurement chưa gửi của đúng `device_id`, còn dữ liệu kênh khác trong cùng file
được giữ lại.

LittleFS tồn tại qua reset/mất nguồn, nhưng tổng chưa đủ thành telemetry 10 phút
chỉ ở RAM và sẽ mất khi mất nguồn. Nạp firmware thông thường giữ NVS/LittleFS;
erase toàn bộ flash sẽ xóa Wi-Fi, Test Information, trạng thái và telemetry chờ.

## Cấu trúc source

| File | Chức năng |
|---|---|
| `src/main.cpp` | Khởi tạo và vòng lặp chính |
| `src/config.h` | GPIO, ADC, ngưỡng pin, MQTT và timing |
| `src/battery_manager.*` | Đọc ADC và quy đổi dữ liệu pin |
| `src/sampling_manager.*` | Lấy mẫu và trung bình 1/10 phút |
| `src/identity_manager.*` | Gateway ID, device ID và boot ID |
| `src/settings_manager.*` | NVS cho test, Wi-Fi và MQTT |
| `src/command_manager.*` | State machine, validation và ACK |
| `src/telemetry_manager.*` | Payload, LittleFS queue và backfill |
| `src/mqtt_manager.*` | TLS MQTT, Last Will, reconnect, PUBACK |
| `src/ap_web_manager.*` | Web AP và API cấu hình/giám sát |
| `src/time_manager.*` | NTP và UTC timestamp |
