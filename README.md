# ID100 Battery Monitor - ESP32-C3 Mini

Firmware đo đồng thời hai pin ID100, hiển thị Web App qua WiFi AP, kết nối router, gửi JSON lên server và hỗ trợ Telegram Bot.

## Đấu nối

| Kênh | Thiết bị | Chân ADC |
|---|---|---|
| 1 | ID100-001 | GPIO1 |
| 2 | ID100-002 | GPIO2 |
| 3 | ID100-003 | GPIO3 |
| 4 | ID100-004 | GPIO4 |

Mỗi đầu vào phải đi qua mạch chia áp; không đưa điện áp pin vượt giới hạn ADC trực tiếp vào ESP32. Firmware mặc định dùng hệ số `1.500` trong `src/config.h`, tương ứng ví dụ ADC 2198 mV thành điện áp pin 3297 mV. Hai mạch phải chung GND với ESP32.

Phần trăm mặc định nội suy tuyến tính từ 2400 mV (0%) tới 3300 mV (100%). Trạng thái gồm: `empty` ở 2400 mV trở xuống, `critical` từ trên 2400 đến dưới 2500 mV, `low` từ 2500 đến dưới 3000 mV, `normal` từ 3000 đến dưới 3300 mV, `full` từ 3300 đến 3450 mV và `over_voltage` trên 3450 mV. Có thể hiệu chỉnh các ngưỡng và `ADC_DIVIDER_RATIO` sau khi đo đối chiếu bằng đồng hồ.

## Sử dụng

1. Nạp firmware từ environment `esp32-c3-mini`.
2. Kết nối AP `ID100-Battery-Monitor`, mật khẩu `12345678`.
3. Mở `http://4.4.4.4`.
4. Nhập người test, địa điểm test, chọn WiFi router trong danh sách tự động quét và nhập mật khẩu.

Cấu hình người test, địa điểm và WiFi được lưu trong NVS.

## MQTT theo RQR server

Firmware dùng MQTT 3.1.1 qua TLS port 8883, QoS 1, retain false, keep-alive 60 giây và clean session false. `gateway_id` và MQTT Client ID được tạo ổn định từ MAC theo dạng `GW-7CDFA1234567`; `boot_id` là UUID mới sau mỗi lần khởi động.

ESP chỉ subscribe `id100/{gateway_id}/+/desired`. Telemetry được publish vào `id100/{gateway_id}/telemetry`, còn ACK vào `id100/{gateway_id}/{device_id}/ack`.

Server điều khiển từng kênh bằng `start`, `pause`, `resume`, `stop`. Chỉ kênh `running` được lấy mẫu. Sau `start`, firmware đo mẫu 1 giây đầu tiên và gửi ngay. Sau đó mỗi giây lấy một mẫu; đủ 60 mẫu thì tạo trung bình một phút; đủ 10 trung bình phút thì tạo và gửi kết quả trung bình 10 phút. Mẫu đầu tiên vẫn được tính vào phút đầu. Một telemetry batch có thể chứa một hoặc hai measurement tùy trạng thái kênh.

Mọi batch được ghi vào LittleFS trước khi gửi. Dữ liệu được gửi theo thứ tự cũ đến mới và chỉ xóa khỏi flash sau MQTT PUBACK. Khi mất mạng, dữ liệu tiếp tục được đo và đánh dấu `is_backfill=true`. Lệnh `stop` xóa dữ liệu chưa gửi của đúng kênh trước khi ACK.

Trước khi chạy, phải thay `MQTT_HOST`, `MQTT_USERNAME`, `MQTT_PASSWORD` và `MQTT_CA_CERT` trong `src/config.h`. Firmware cố ý không kết nối nếu host còn là placeholder hoặc CA rỗng. MQTT host/user/password được khởi tạo vào NVS ở lần chạy đầu.

## Telegram (chạy nền)

- `/status`: toàn bộ dữ liệu và trạng thái ESP32
- `/pin1`: ID100-001 / GPIO3
- `/pin2`: ID100-002 / GPIO4
- `/help`: danh sách lệnh

Chỉ `chat_id` đã cấu hình được xử lý.
Token và Chat ID là cấu hình cố định trong `src/config.h`; phần Telegram không xuất hiện trên Web App.

## Build

```powershell
./scripts/platformio-env.ps1 build
```
