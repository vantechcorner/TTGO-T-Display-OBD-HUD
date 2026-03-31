# TTGO T-Display OBD HUD

> **Bản dịch của [README.md](README.md)** (tiếng Anh). Khi README gốc được sửa, file này nên được cập nhật tương ứng.

Firmware cho **LilyGO TTGO T-Display** (ESP32, ST7789 135×240, USB‑C): kết nối **Bluetooth Classic** tới adapter **ELM327** OBD-II và hiển thị bảng điều khiển **LVGL 9** (tốc độ quay máy, vận tốc, nhiệt độ làm mát, điện áp ắc quy). **V1.2** có **tùy chọn khi build để đổi vị trí RPM và tốc độ** trên màn hình (xem **`HUD_SWAP_RPM_SPEED_LAYOUT`**). Đèn nền vẫn chỉnh bằng **nút GPIO35** (100% / 50% / 20%), được đọc trên **task FreeRTOS riêng** nên vẫn hoạt động khi Bluetooth đang chờ kết nối lâu và **`loop()`** bị chặn. Với **`HUD_USE_BUTTON_BACKLIGHT=0`**, đèn nền giữ **mức PWM lúc khởi động** (thường là sáng tối đa) và firmware **không** đọc nút.

### Phiên bản firmware


| Phiên bản | Ghi chú                                                                                       |
| --------- | --------------------------------------------------------------------------------------------- |
| **V1.0**  | Nền tảng ổn định: đèn nền cố định, giao diện đầy đủ thông số.                                |
| **V1.1**  | **Nút điều chỉnh đèn nền:** nhấn ngắn **BUTTON1 (GPIO35)** để chuyển PWM **100% → 50% → 20% → 100%** (chống dội). Bảng chân LilyGO: **BUTTON1 = 35**, **BUTTON2 = 0** ([pinmap](https://raw.githubusercontent.com/Xinyuan-LilyGO/TTGO-T-Display/master/image/pinmap.jpg)). |
| **V1.2**  | **Bố cục:** bật **`HUD_SWAP_RPM_SPEED_LAYOUT=1`** để **tốc độ số lớn ở giữa** kèm chữ **`km/h`** nhỏ, **RPM** (có nhãn **`RPM`**) **dưới trái**; **thanh RPM** giữ nguyên. Cần font **Montserrat 28** trong `lv_conf.h` (repo đã bật). **Đèn nền:** đọc GPIO35 trên **core 0** để bước sáng và log **`HUD_SERIAL_DEBUG_BACKLIGHT`** không bị đứng hàng chục giây khi **`SerialBT.connect()`** chờ; in debug **không chặn TX** khi buffer UART USB đầy. |


## Adapter đã thử (cấu hình tham chiếu)

Dự án dành cho adapter có **Bluetooth Classic (BR/EDR, “BT 3.0”)** — **không** phải Bluetooth Low Energy (BLE).

**Phần cứng tham chiếu:** **Vgate iCar Pro Bluetooth 3.0** — chỉ Bluetooth Classic (không BLE). Ghép nối theo **tên thiết bị Bluetooth** và/hoặc **MAC**, giống các dongle ELM327 serial khác.

> **Lưu ý ESP32:** ESP32 hỗ trợ Bluetooth Classic nên mới nói chuyện được với loại dongle này. Board **chỉ có BLE** không chạy firmware này với ELM327 BT 3.0 trừ khi có **cầu nối** khác.

## Đổi tên Bluetooth của adapter

Firmware mở kết nối **BluetoothSerial** bằng **tên** (và tùy chọn **MAC**) đã gắn sẵn trong file nhị phân.

1. Mở `platformio.ini` ở thư mục gốc project.
2. Trong `[env:ttgo_t_display]` → `build_flags`, tìm dòng:
  ```ini
   -D OBD_BT_NAME=\"YourAdapterName\"
  ```
3. Thay `YourAdapterName` bằng **đúng tên Bluetooth** điện thoại/PC hiển thị khi quét (thực tế phân biệt hoa thường; firmware chuẩn hóa phần trả lời nhưng mục tiêu ghép nối lúc đầu là tên bạn cấu hình).
4. **Tùy chọn — MAC cố định:** Nếu nhiều thiết bị trùng tên hoặc ghép nối không ổn, đặt:
  ```ini
   -D OBD_BT_MAC=\"AA:BB:CC:DD:EE:FF\"
  ```
   Dùng MAC thật của adapter (dấu hai chấm, hex in hoa thường gặp). Có thể để `OBD_BT_MAC` là `\"\"` để chỉ kết nối theo tên (thứ tự kết nối xem `src/main.cpp`).
5. **Build lại và nạp lại** sau mỗi thay đổi (`pio run -e ttgo_t_display -t upload`).

Mặc định trong repo có thể vẫn là tên giữ chỗ (ví dụ `V-LINK`); **bạn phải chỉnh `OBD_BT_NAME` khớp dongle Vgate (hoặc hãng khác) của bạn.**

## Yêu cầu

- [PlatformIO](https://platformio.org/) (mở rộng VS Code hoặc CLI)
- LilyGO TTGO T-Display
- Adapter ELM327 **Bluetooth Classic** (ví dụ Vgate iCar Pro BT 3.0)

## Build và nạp firmware

Môi trường mặc định: `ttgo_t_display` (board `esp32dev`, phân vùng `huge_app` vì kích thước firmware).

### Dùng VS Code

1. Cài [Visual Studio Code](https://code.visualstudio.com/).
2. Mở Extensions (**Ctrl+Shift+X** / **Cmd+Shift+X**), tìm **PlatformIO IDE**, cài đặt, rồi **reload** cửa sổ khi được hỏi (lần đầu có thể tải PlatformIO Core / toolchain — đợi đến khi thanh trạng thái báo PlatformIO sẵn sàng).
3. **File → Open Folder…** và chọn **thư mục gốc project** (thư mục có `platformio.ini`).
4. Đợi PlatformIO lập chỉ mục xong; xác nhận môi trường đang dùng là **`ttgo_t_display`** (danh sách env nằm trên thanh công cụ PlatformIO phía dưới — chọn `ttgo_t_display` nếu có nhiều env).
5. **Build:** biểu tượng PlatformIO trên thanh trái → **PROJECT TASKS** → **ttgo_t_display** → **General** → **Build**, hoặc nút **dấu kiểm** (“PlatformIO: Build”) trên thanh trạng thái dưới cùng.
6. **Upload:** Cắm board qua USB, rồi **General** → **Upload**, hoặc nút **mũi tên** (“PlatformIO: Upload”). Nếu cần, đưa board vào chế độ download (TTGO T-Display thường nạp bình thường không cần giữ nút).
7. **Serial Monitor:** **General** → **Monitor**, hoặc biểu tượng **phích cắm** (“PlatformIO: Serial Monitor”) — baud **115200** đã cấu hình trong `platformio.ini`.

Nếu **Upload** hoặc **Monitor** lỗi, cài driver USB-UART cho OS (CP210x hoặc CH340 tùy board) và chọn đúng cổng COM trong **PlatformIO: Upload Port** / danh sách thiết bị.

### Dòng lệnh (CLI)

```bash
pio run -e ttgo_t_display -t upload
```

Serial monitor (115200 baud):

```bash
pio device monitor -b 115200
```

### Ghi chú bảo trì (tài liệu)

Khi sửa **README.md** (tiếng Anh), hãy cập nhật **[README-VN.md](README-VN.md)** để bản tiếng Việt luôn khớp.

## Tóm tắt cấu hình (`platformio.ini`)


| Tham số `build_flags`      | Mục đích                                                                                      |
| -------------------------- | --------------------------------------------------------------------------------------------- |
| `OBD_BT_NAME`              | Tên Bluetooth của adapter ELM327 (phải khớp dongle)                                           |
| `OBD_BT_MAC`               | MAC cố định tùy chọn nếu chỉ theo tên không ổn định                                           |
| `BRIDGE_RPM_MS`, `BRIDGE_SPEED_MS`, … | Chu kỳ hỏi PID (ms); tên lịch sử, chỉ định thời gian trong `src/main.cpp`          |
| `HUD_USE_BUTTON_BACKLIGHT` | Mặc định `1`: nhấn ngắn GPIO35 chuyển đèn nền **100% / 50% / 20%**. Đặt `0` để tắt đọc nút; đèn nền giữ mức lúc boot. |
| `HUD_BL_BUTTON_PIN`        | Chân GPIO nút (mặc định **35** = **BUTTON1** LilyGO).                                         |
| `HUD_BL_BUTTON_ACTIVE_HIGH`| Mặc định `0` (nhấn = LOW). Đặt `1` nếu nhấn đọc HIGH.                                       |
| `HUD_LEDC_WRITE_USES_GPIO_PIN` | Thường tự nhận theo Arduino core: `0` = `ledcWrite(channel, …)` (core 2.x), `1` = `ledcWrite(GPIO, …)` (core 3.x). Chỉnh tay nếu sau nâng core đèn nền không đổi. |
| `HUD_SERIAL_DEBUG_BACKLIGHT` | Thêm define (không giá trị) để in dòng GPIO/PWM lên **Serial** 115200 baud.                 |
| `HUD_SWAP_RPM_SPEED_LAYOUT`  | Mặc định `0`. Đặt `1`: **tốc độ** căn giữa — **số lớn + chữ `km/h` nhỏ** (hàng flex); **RPM** **dưới trái** — **số font 28 + chữ `RPM` nhỏ**. **Thanh RPM** không đổi. |


Chân màn hình và tùy chọn **TFT_eSPI** cho board TTGO cũng nằm trong `platformio.ini`.

**Đèn nền:** PWM trên **`TFT_BL`** (chân **4** trong bản build này). Mặc định **`HUD_BL_BUTTON_PIN`** là **35** (**BUTTON1**): pull-up 3,3 V, **nhấn = LOW**. Dùng **BUTTON2** thì đặt `HUD_BL_BUTTON_PIN=0` (lưu ý: giữ **GPIO0** thấp lúc reset sẽ vào chế độ download). Cần có `TFT_BL` trong `platformio.ini` (đã bật cho TTGO T-Display). Sau khi khởi tạo **LVGL** và **Bluetooth**, firmware **gắn lại LEDC** trên `TFT_BL` để chân không bị kẹt ở chế độ GPIO thường. Nếu nhấn nút mà độ sáng vẫn không đổi, hãy thử đảo **`HUD_LEDC_WRITE_USES_GPIO_PIN`** (`0` / `1`) cho đúng core Arduino-ESP32 của bạn.

### Nút đèn nền: Serial Monitor (xử lý sự cố)

1. Thêm vào `build_flags` trong `platformio.ini`: `-D HUD_SERIAL_DEBUG_BACKLIGHT`
2. Build lại, nạp, mở serial monitor **115200** baud.
3. Sẽ thấy dòng khởi động với `TFT_BL`, GPIO nút, và `ledc_writes_pin` hay `ledc_writes_channel`. Khoảng mỗi 400 ms: `GPIO35 raw=…` (0 = đang nhấn trên board điển hình), `step`, `duty`.
4. Nếu nhấn **GPIO35** mà **`step` / `duty` không đổi** nhưng `raw` đảo — xem dòng `ledc_writes_*`; sau khi lên **Arduino-ESP32 3.x** có thể cần `-D HUD_LEDC_WRITE_USES_GPIO_PIN=1` (hoặc `0` nếu trước đó sai).
5. Nếu **`raw` không đổi** khi nhấn hai nút trên board, thử `-D HUD_BL_BUTTON_PIN=0` cho **BUTTON2**, hoặc `-D HUD_BL_BUTTON_ACTIVE_HIGH=1` nếu phần cứng đảo mức.
6. Nếu **`raw` luôn là `1` và `released=1`** dù bạn đang nhấn **BUTTON1**, vi điều khiển **không bao giờ thấy mức LOW** trên chân đó (sai chân, nút hỏng, hoặc board clone thiếu pull-up ngoài trên **GPIO35** — ESP32 **không** bật được pull-up nội cho GPIO **34–39**). Hãy thử **`HUD_BL_BUTTON_PIN=0`** (nút kia; đừng giữ thấp lúc reset), đối chiếu [pinmap LilyGO](https://raw.githubusercontent.com/Xinyuan-LilyGO/TTGO-T-Display/master/image/pinmap.jpg), hoặc hàn **10kΩ lên 3,3V** nếu chân đang “trôi”.

### Serial: `ASSERT_WARN(… lc_task.c …)` lúc khởi động

Các dòng **`ASSERT_WARN`** dài từ **`lc_task.c`** đến từ **stack Bluetooth** ESP32 (Bluedroid), không phải parser OBD. Thường thấy khi bật BT Classic và **thường không chết** nếu adapter vẫn kết nối và thông số vẫn cập nhật.

## Cấu trúc thư mục


| Đường dẫn                 | Vai trò                                                                       |
| ------------------------- | ----------------------------------------------------------------------------- |
| `src/main.cpp`            | Client ELM327 Bluetooth, parse OBD, giao diện LVGL                          |
| `include/lv_conf.h`       | Cấu hình LVGL và font bật                                                     |
| `images/elm_327_mini.png` | Ảnh tham chiếu cho tài liệu                                                   |


## Tùy chọn: kiểm tra dòng OBD ngoại tuyến

`tools/obd_normalizer.py` phát lại các dòng serial đã bắt để kiểm tra hex / parse mode `01` (không cần xe hay nạp chip).

## Xe đã thử

Firmware đã **chạy thành công** trên **Mazda 2**, **hộp số tự động**, đời **2023**, với adapter tham chiếu nêu trên.

## Credits

- **Tác giả:** Van Tech Corner  
- **Trợ lý:** Cursor AI  
- **Ý tưởng:** dự án [Car HUD](https://github.com/fbiego/car-hud) của [fbiego](https://github.com/fbiego) — khái niệm HUD OBD ESP32 + LVGL (upstream tập trung BLE và stack hiển thị khác; repo này nhắm TTGO T-Display và adapter ELM327 **Bluetooth Classic**).

## Giấy phép

Xem `LICENSE`.
