# CAN – SPI HB2000: bảng lệnh và dữ liệu phản hồi V2

Bản này giữ GPIO theo yêu cầu: PTD1 chọn chiều, timer bật/tắt PTD15 vào IN2.
Dùng SysTick 1 ms đang có; PTD15 vẫn MUX=GPIO, không dùng LPIT hay ngõ ra FTM.
Mỗi 4 nhịp bật 0/1/2/3/4 nhịp, tạo các mức 0/25/50/75/100% gần đúng.
Không có KEEPALIVE, số thứ tự sequence hay xác nhận từ PC.
Gửi START/thuận/nghịch một lần sẽ giữ chạy đến khi STOP hoặc có lỗi chặn.

**Giao thức đã đổi so với V1:** byte đầu giờ là opcode.
Hãy dùng các frame bên dưới, không dùng lại bảng lệnh V1.

## 1. Cấu hình CAN và cấu trúc lệnh

| Thông số | Giá trị |
| --- | --- |
| Loại CAN | Classic CAN, standard ID 11 bit, data frame |
| Bitrate | 500 kbit/s |
| ID PC → S32K144 | 0x100 |
| ID S32K144 → PC | 0x180 |
| DLC | 8 |
| Cách viết ví dụ | HEX |

Payload PC gửi:

    B0      B1          B2          B3 B4 B5 B6 B7
    opcode  argumentLo  argumentHi  00 00 00 00 00

Chỉ opcode 04 dùng argument. Các opcode còn lại yêu cầu B1…B7 đều bằng 00.
Ví dụ 1000 = 0x03E8 được gửi byte thấp trước: E8 03.
DLC sai, opcode không có hoặc các byte dự phòng khác 00 sẽ bị từ chối.

## 2. Các message điều khiển cơ bản

Tất cả gửi ID 0x100, DLC=8.

| Chức năng | Data HEX | Kết quả |
| --- | --- | --- |
| STOP | 00 00 00 00 00 00 00 00 | Ngắt IN2, tắt EN qua SPI |
| START | 01 00 00 00 00 00 00 00 | Chạy theo chiều đã chọn, mặc định thuận |
| Thuận | 02 00 00 00 00 00 00 00 | Chọn thuận và chạy |
| Nghịch | 03 00 00 00 00 00 00 00 | Chọn nghịch và chạy |
| Mức 25% | 04 FA 00 00 00 00 00 00 | Duty 250/1000 theo chiều đã chọn |
| Mức 50% | 04 F4 01 00 00 00 00 00 | Duty 500/1000 theo chiều đã chọn |
| Mức 75% | 04 EE 02 00 00 00 00 00 | Duty 750/1000 theo chiều đã chọn |
| Mức chạy 1000/1000 | 04 E8 03 00 00 00 00 00 | Chạy theo chiều đã chọn |
| Mức 0 | 04 00 00 00 00 00 00 00 | Dừng |
| CLEAR_FAULT | 05 00 00 00 00 00 00 00 | Xóa lỗi nếu điều kiện gây lỗi đã hết; giữ dừng |
| GET_STATUS | 06 00 00 00 00 00 00 00 | Gửi lại dữ liệu trạng thái/thanh ghi gần nhất |

Không cần gửi lặp. Gửi lặp cùng lệnh đang thực hiện không làm khởi động lại quá
trình đó. Lệnh điều khiển mới thay thế lệnh thường đang chờ; STOP luôn ưu tiên.
Giữ nút STOP vật lý sẽ chặn mọi lệnh chạy.

Opcode 04 nhận argument 0…1000, quy đổi về mức gần nhất trong 0, 250, 500,
750, 1000. Giá trị đúng giữa hai mức được làm tròn lên. Cụ thể:

| Argument nhận (phần nghìn) | Duty áp dụng |
| --- | --- |
| 0…124 | 0% (dừng) |
| 125…374 | 25% |
| 375…624 | 50% |
| 625…874 | 75% |
| 875…1000 | 100% |

Giá trị >1000 bị từ chối với mã 81. Ví dụ 30% (`04 2C 01…`) thành 25%;
70% (`04 BC 02…`) thành 75%. Duty không phải phần trăm RPM đã đo.
START/thuận/nghịch vẫn chọn duty 100%; SET_DUTY chạy theo chiều đã chọn.

Ví dụ thao tác độc lập, không cần KEEPALIVE:

    02 00 00 00 00 00 00 00   chạy thuận, giữ chạy
    03 00 00 00 00 00 00 00   chuyển sang nghịch, giữ chạy
    00 00 00 00 00 00 00 00   dừng

Khi đang chạy mà đổi chiều, ứng dụng hạ IN2 và xác minh EN=0, chờ khoảng ngắt
100 ms tính từ lúc thu hồi điều khiển đang chạy, rồi mới đổi IN1 và bật lại.
Đây là khoảng ngắt điện điều khiển, không phải phép đo trục đã đứng yên.

## 3. CLEAR_FAULT thực hiện những gì?

1. Hạ IN2, giữ đầu ra không chạy.
2. Khởi tạo lại SPI và đọc mới ID/config HB2000.
3. Ghi EN=0, đọc lại để xác minh, chờ ổn định.
4. Gửi từ SPI 0xAFFF để xóa các cờ có thể xóa.
5. Đọc mới Status. Nếu các cờ chặn đã hết thì bỏ chốt lỗi phần mềm.
6. Giữ STOPPED; chờ một lệnh chạy mới. Chiều sau phục hồi là thuận.

Nếu CP_U/UV/OV/ngắn mạch/OT hoặc lỗi SPI vẫn còn, lệnh trả lỗi và motor vẫn bị
khóa. STOP có thể hủy quá trình phục hồi. CAN bus-off hay driver không nhận được
frame cần được khắc phục và khởi động lại; CLEAR_FAULT không bỏ qua lỗi đang có.

## 4. Các loại phản hồi, ID 0x180

Byte đầu xác định loại. Byte cuối bằng 02 là phiên bản giao thức V2.

| B0 | Loại |
| --- | --- |
| 01 | Kết quả lệnh, tên trong code là ACK_NACK |
| 02 | MOTOR_STATUS |
| 03 | MOTOR_ERROR |
| 04 | HB_DIAGNOSTICS |

PC chỉ cần gửi và nhận frame bình thường. Message 01 do MCU tạo ra; MCU không
đợi PC xác nhận ngược lại. Ứng dụng chỉ gửi kết quả hoàn tất hoặc từ chối,
không phát thêm phản hồi PENDING cho mỗi lệnh.

### 4.1. Kết quả lệnh

    B0 B1 B2      B3      B4     B5     B6     B7
    01 00 result  opcode  argLo  argHi  fault  02

| Result | Ý nghĩa |
| --- | --- |
| 00 | OK, ví dụ GET_STATUS |
| 02 | COMPLETED: GPIO/config đã được xác minh |
| 80 | Opcode/định dạng không hợp lệ |
| 81 | Argument ngoài 0…1000 hoặc byte dự phòng/argument opcode khác không hợp lệ |
| 82 | Trạng thái không cho phép thực hiện |
| 83 | Chưa khởi tạo được backend |
| 84 | Lỗi điều khiển motor/HB2000 |
| 85 | Đang bận |
| 87 | Đang bị khóa, ví dụ giữ STOP hoặc lỗi chưa được xóa |
| 88 | Lỗi tầng CAN |
| 89 | Timeout nội bộ |

Một số mã giữ trong enum để tương thích nội bộ nhưng không có đường sinh bình
thường ở V2; không còn mã xung đột sequence vì request không có sequence.

Ví dụ chạy thuận hoàn tất:

    01 00 02 02 00 00 00 02

Ví dụ đặt mức 30% được quy đổi thành 25%, phản hồi sau khi áp dụng:

    TX ID100: 04 2C 01 00 00 00 00 00
    RX ID180: 01 00 02 04 FA 00 00 02

ACK dùng argument sau quy đổi; các byte gốc vẫn được giữ nội bộ để nhận biết
lệnh gửi lặp, không làm khởi động lại cùng một lệnh đang thực hiện.

Phản hồi dùng bộ đệm mới nhất. Khi gửi dồn nhiều lệnh trong lúc TX bận, có thể
mất phản hồi cũ; việc điều khiển và STOP không chờ bộ đệm gửi hay xác nhận PC.

### 4.2. MOTOR_STATUS

    B0 B1 B2     B3     B4              B5              B6     B7
    02 00 state  flags  requestedDutyLo requestedDutyHi fault  02

| State | Ý nghĩa |
| --- | --- |
| 00 | UNCONFIGURED |
| 01 | STOPPED |
| 02 | STARTING |
| 03 | RUNNING |
| 04 | STOPPING |
| 05 | FAULT |

| Bit flags | Mask | Ý nghĩa khi bằng 1 |
| --- | --- | --- |
| 0 | 01 | Backend đã khởi tạo |
| 1 | 02 | Đã xác minh GPIO dừng và EN=0, không có lỗi app |
| 2 | 04 | Dự phòng, luôn 0 |
| 3 | 08 | Có hỗ trợ chạy nghịch |
| 4 | 10 | Cho phép điều khiển theo timebase/lỗi/khóa hiện tại; KHÔNG còn là KEEPALIVE |
| 5 | 20 | Có lệnh đang chờ/thực hiện |
| 6 | 40 | Chiều điều khiển hiện tại là nghịch |
| 7 | 80 | Dự phòng |

requestedDuty là mức sau quy đổi, không phải RPM hay phép đo tốc độ.
STOP thông thường có thể giữ requestedDuty ở mức trước đó nhưng đầu ra đã ngắt.
SET_DUTY=0 đặt requestedDuty=0; START tiếp theo đặt lại mức chạy 1000.

Ví dụ khi không còn lệnh đang chờ:

| Data HEX | Ý nghĩa |
| --- | --- |
| 02 00 01 1B E8 03 00 02 | STOPPED, đã xác minh ngắt, không lỗi |
| 02 00 03 19 E8 03 00 02 | RUNNING thuận |
| 02 00 03 59 E8 03 00 02 | RUNNING nghịch |

RUNNING xác nhận lệnh GPIO/config đã áp dụng, không xác nhận trục đang quay.

### 4.3. MOTOR_ERROR và mã fault

    03 00 fault flags requestedDutyLo requestedDutyHi fault 02

| Fault | Ý nghĩa |
| --- | --- |
| 00 | Không lỗi |
| 02 | Backend: HB2000, SPI, readback/config/GPIO |
| 03 | API CAN |
| 04 | Bus-off |
| 05 | RX overrun |
| 08 | Timebase không hợp lệ |

Mã 01 là mã timeout liên lạc cũ, V2 không còn sinh lỗi do thiếu KEEPALIVE.
Các mã enum khác là dự phòng/không có đường sinh bình thường trong app này.
g_Fault trong debugger là mã chẩn đoán nội bộ, không cùng bảng với byte fault CAN:
1=SPI API, 2=readback/protocol/GPIO, 4=cờ chặn HB, 5=time, 8=khóa từ supervisor,
9=khởi tạo bộ phát PWM phần mềm.
main.c còn dùng các mã riêng 3/6/7 cho khởi tạo clock/pin/interrupt.

### 4.4. HB_DIAGNOSTICS

    B0 B1     B2        B3        B4        B5        B6     B7
    04 valid  statusLo  statusHi  configLo  configHi  idLow  02

Status và Config là toàn bộ từ SPI 16 bit, gồm tag thanh ghi.
CAN gửi byte thấp trước. idLow chỉ là byte thấp của Device ID.
valid=1 nghĩa là có mẫu đọc hợp lệ; vẫn có thể chứa cờ lỗi.
valid không phải timestamp và không bảo đảm vừa đọc ở thời điểm CAN được nhận.

| Data HEX | Giải mã |
| --- | --- |
| 04 01 00 20 88 6D 11 02 | Status=2000, Config=6D88, EN=0 |
| 04 01 00 20 98 6D 11 02 | Status=2000, Config=6D98, EN=1 |
| 04 01 00 24 88 6D 11 02 | Status=2400: CP_U; EN=0 |
| 04 01 08 20 98 6D 11 02 | Status=2008: OL, tiếp tục báo chẩn đoán |

## 5. Giải mã thanh ghi Status SPI

Tách cờ: hbFaultBits = hbStatus & 0x0FFF.
0x2000 là tag của Status; riêng giá trị này không phải lỗi.

| Bit | Mask | Cờ | Chính sách app GPIO |
| --- | --- | --- | --- |
| 11 | 0800 | FRM, lỗi khung SPI | Chặn/dừng |
| 10 | 0400 | CP_U, charge pump thấp | Chặn/dừng |
| 9 | 0200 | UV, VPWR thấp | Chặn/dừng |
| 8 | 0100 | OV, VPWR cao | Chặn/dừng |
| 7 | 0080 | SCP2 | Chặn/dừng |
| 6 | 0040 | SCP1 | Chặn/dừng |
| 5 | 0020 | SCG2 | Chặn/dừng |
| 4 | 0010 | SCG1 | Chặn/dừng |
| 3 | 0008 | OL, hở tải | Báo qua CAN |
| 2 | 0004 | OC, đã kích hoạt giới hạn dòng | Báo qua CAN |
| 1 | 0002 | TW, cảnh báo nhiệt | Báo qua CAN |
| 0 | 0001 | OT, quá nhiệt | Chặn/dừng |

Mask các cờ chặn là 0x0FF1. Cách xử lý OL/OC/TW đã đổi so với V1.
Không tắt bảo vệ phần cứng: AL/thermal/current-limit của HB2000 vẫn hoạt động.
Status là cờ chẩn đoán, không đo được ampere, nhiệt độ hay RPM.

## 6. Config, chân điều khiển và FS_B

Config có tag 0x6000; dữ liệu là config & 0x1FFF.
EN=bit4, MODE=bit3, INPUT=bit2. App chọn MODE=1 và INPUT=0 để dùng chân IN1/IN2.

| Trạng thái | PTD1 → IN1 | PTD15 → IN2 | EN SPI |
| --- | --- | --- | --- |
| Thuận | 1 | PWM theo duty (100% = 1) | 1 |
| Nghịch | 0 | PWM theo duty (100% = 1) | 1 |
| STOP đã xác minh | Giữ chiều | 0 | 0 |

ENBL kéo cao bằng jumper không ghi đè bit EN=0 qua SPI.
FS_B active low; Disable/EN=0 cũng có thể làm LED FS_B đỏ sáng.
Không dùng riêng màu LED để kết luận một cờ lỗi cụ thể.

Giữ cách nối SPI: PTB2=SCK, PTB3=MISO, PTB4=MOSI, PTB5=CS_B, chung GND.
CAN0: PTE5 TX → TXD transceiver, PTE4 RX ← RXD; CANH/CANL qua transceiver.

Theo guide board đã gửi:
- Dây IN1/IN2 vào J10 chân 1/3: J1 và J3 đặt 1–2.
- Dây IN1/IN2 vào J15 chân 1/2: J1 và J3 đặt 2–3.
- J2 DIS=2–3, J5 ENBL=2–3, J19 FS_B=1–2.

IC thực tế được xác nhận là MC33HB2000; guide dùng để đối chiếu connector/jumper.

## 7. Chu kỳ và dữ liệu chia sẻ

SPI: mode 1 (CPOL=0, CPHA=1), MSB first, 16 bit, khoảng 1 MHz.
Lệnh đọc dùng hai frame riêng CS để lấy đúng thanh ghi theo pipeline.
Bộ đệm driver là uint16 native; không đổi byte trong bộ đệm SPI.

Status được đọc khoảng mỗi 50 ms, cả lúc FAULT. ID đọc khi khởi tạo/phục hồi;
Config đọc lại khi bật/tắt EN. AppShared giữ bản snapshot cho CAN.
CAN loại 02 và 04 lập lịch mỗi 100 ms, gửi lần lượt trên cùng ID 180.
Phần mềm tính cycle theo ID có thể thấy một khoảng ngắn giữa hai loại này.
GET_STATUS lấy cache; không ép SPI đọc mới ngay tại thời điểm nhận lệnh.

Không có timeout vì PC ngừng gửi. Giá trị khởi tạo cũ 1000 trong main.c được
CanApp_Init V2 bỏ qua. Bộ đếm millisecond chỉ phục vụ debounce, SPI settling,
thời gian đổi chiều và lịch telemetry.

## 8. Nguồn đối chiếu và build

- [MC33HB2000 datasheet](https://www.nxp.com/docs/en/data-sheet/MC33HB2000.pdf), bảng thanh ghi, bảng 21/22.
- [Guide board](https://www.nxp.com/docs/en/user-guide/KTFRDMHB2001EVMUG.pdf), bảng jumper/connector.
- [Codec](../protocol/can_protocol.c).
- [CAN app](can_app.c).
- [Motor app](spi_motor_app.c).
- [GPIO/SPI app](hb2000_io.c).

Refresh project trong S32DS rồi Clean/Build để dùng mã tầng app mới.
Không cần sửa main.c, driver hoặc cấu hình sinh sẵn. Kiểm tra build/mô phỏng
không thay thế việc nạp và đo tín hiệu trên board thật.
