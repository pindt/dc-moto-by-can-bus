# Ứng dụng CAN – SPI – HB2000, giao thức V2, điều khiển GPIO

Chỉ tầng ứng dụng trong src/application và src/protocol được thay đổi ở lần này.
main.c, driver CAN/SPI, RTD và cấu hình sinh sẵn được giữ nguyên.

## Gửi lệnh nhanh

Classic CAN, 500 kbit/s, ID chuẩn 11 bit, data frame, DLC=8.
Máy tính gửi ID 0x100; MCU trả ID 0x180. Các số dưới đây là HEX.

| Chức năng | Đủ 8 byte gửi tới ID 100 |
| --- | --- |
| STOP | 00 00 00 00 00 00 00 00 |
| START theo chiều đã chọn, mặc định thuận | 01 00 00 00 00 00 00 00 |
| Chạy thuận ngay | 02 00 00 00 00 00 00 00 |
| Chạy nghịch ngay | 03 00 00 00 00 00 00 00 |
| Xóa lỗi, giữ động cơ dừng | 05 00 00 00 00 00 00 00 |
| Đọc trạng thái | 06 00 00 00 00 00 00 00 |

Gửi một lệnh chạy là giữ chạy liên tục. Không có KEEPALIVE, sequence hay yêu cầu
máy tính xác nhận phản hồi. Không gửi lại các frame V1 có sequence ở byte đầu:
V2 dùng byte đầu làm opcode, nên ý nghĩa frame đã thay đổi.

PTD1 → IN1 và PTD15 → IN2 đều là GPIO. Không dùng timer/PWM.
Vì vậy chỉ có chạy toàn mức điều khiển hoặc dừng; không có tốc độ trung gian/RPM.
Opcode 04 chỉ chấp nhận:
- 04 E8 03 00 00 00 00 00: mức 1000/1000, chạy theo chiều đã chọn.
- 04 00 00 00 00 00 00 00: mức 0, dừng.
Giá trị 1…999 bị từ chối với mã 81; không đổi trạng thái đang chạy.

## Hành vi

- START/thuận/nghịch không cần CAN gửi định kỳ. Lệnh chạy đến trong lúc khởi tạo
  được giữ lại và thực hiện khi HB2000 sẵn sàng, nếu không có lỗi.
- Thuận: PTD1=1, PTD15=1; nghịch: PTD1=0, PTD15=1.
- STOP hạ PTD15 trước rồi tắt EN qua SPI và đọc lại cấu hình.
- Khi đang chạy mà đổi chiều: hạ IN2, xác minh EN=0, chờ khoảng ngắt 100 ms,
  đổi IN1 rồi bật EN và IN2. Khoảng này không phải phép đo trục đã đứng yên.
- PTC12 START, PTC13 STOP, nhấn mức cao; START không cần máy tính duy trì CAN.
  STOP đang giữ sẽ chặn chạy; nhả STOP không tự khởi động lại.
- CLEAR_FAULT hạ IN2, khởi tạo lại SPI, đọc ID/config, xác minh EN=0,
  gửi SPI AFFF rồi đọc mới Status. Chỉ bỏ chốt lỗi khi các cờ chặn đã hết.
  Thành công vẫn ở STOPPED; cần lệnh chạy mới. Chiều sau CLEAR_FAULT là thuận.
- STOP ưu tiên hơn cả lệnh chạy và quá trình xóa lỗi.
- OL, OC, TW tiếp tục được báo qua CAN nhưng không tự chốt dừng ở tầng app;
  chức năng bảo vệ/giới hạn dòng của HB2000 vẫn giữ nguyên.
- CP_U, UV, OV, ngắn mạch, quá nhiệt OT, lỗi SPI/time/CAN vẫn ngắt động cơ.
  Lỗi phần cứng còn tồn tại sẽ không được bỏ qua bởi CLEAR_FAULT.
- CAN bus-off/driver không còn nhận được lệnh không thể phục hồi chỉ bằng một
  frame CAN; phải khắc phục đường truyền và khởi động lại hệ thống.
- main.c còn truyền tham số khởi tạo cũ 1000 vào CanApp_Init; tầng app V2 bỏ qua
  tham số này. Nó không còn là timeout và không làm động cơ dừng sau 1 giây.

## Phản hồi và tài liệu

MCU gửi trạng thái motor và thanh ghi HB định kỳ 100 ms; đọc Status SPI khoảng
50 ms, gồm cả lúc đang chốt lỗi. Phản hồi kết quả lệnh được gửi khi hoàn tất/từ
chối; không gửi thêm PENDING. MCU không đợi PC xác nhận. Khi TX bận, điều khiển
và STOP vẫn chạy; bộ đệm phản hồi chỉ giữ kết quả mới nhất, không bảo đảm giữ
mọi phản hồi nếu người dùng gửi dồn nhiều lệnh.

Xem [GIAI_THICH_CAN_SPI.md](GIAI_THICH_CAN_SPI.md) để biết từng byte, mã lỗi,
các ví dụ CAN V2 và cách nối chân.

## Build và giới hạn xác minh

Các file .c nằm dưới src để S32DS phát hiện khi Refresh/Build.
Không cần sửa driver hay generated files. Nếu IDE còn dùng danh sách build cũ,
Refresh project rồi Clean/Build để sinh lại danh sách nguồn.

Kiểm tra bằng ARM GCC và mô phỏng ứng dụng với CAN/HB giả lập; chưa nạp và đo
trên board thật. Trạng thái RUNNING phản ánh GPIO/config đã xác minh, không đo
tốc độ hoặc xác nhận trục quay.
