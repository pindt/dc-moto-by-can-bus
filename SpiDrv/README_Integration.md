# SpiDrv – hướng dẫn tích hợp (S32K144, RTD 4.4.x IP layer)

## 1. Cấu trúc
```
SpiDrv/
├── include/  SpiDrv.h  SpiDrv_Types.h  SpiDrv_Cfg.h  SpiDrv_Internal.h
│             SpiDrv_Backend.h  SpiDrv_Platform.h  SpiDrv_Baud.h
├── src/      SpiDrv.c            core: state machine, guard BUSY, callback, lỗi
│             SpiDrv_Lpspi.c      backend: gọi Lpspi_Ip_* (file duy nhất include Lpspi_Ip.h)
│             SpiDrv_Platform.c   adapter: Clock_Ip, IntCtrl_Ip, SchM_Spi
│             SpiDrv_Baud.c       tính PRESCALE/SCKDIV/delay (thuần tính toán, test được trên host)
├── cfg/      SpiDrv_PBcfg.c/.h   cấu hình ứng dụng (ví dụ LPSPI0 slave như example NXP)
├── example/  main_spidrv.c       thay main.c của example: phía slave đi qua SpiDrv
└── test/     SpiDrv_Baud_Test.c  unit test host cho SpiDrv_Baud
```

## 2. Thêm vào project S32DS
1. Include path: `SpiDrv/include`, `SpiDrv/cfg` (giữ nguyên `RTD/include`, `generate/include`).
2. Thêm vào build: 4 file trong `src/` và `cfg/SpiDrv_PBcfg.c`.
3. Chỉnh `cfg/SpiDrv_PBcfg.c` cho đúng tên đối tượng do Peripherals tool sinh trong project của bạn
   (`Lpspi_Ip_PhyUnitConfig_...`, `Lpspi_Ip_DeviceAttributes_...`).
4. Trong Peripherals tool phải bật đúng instance LPSPI (macro `LPSPI_IP_n_ENABLED`) – `SpiDrv_Platform.c` chỉ gắn ISR cho instance đã bật.

## 3. Thứ tự khởi tạo
```c
Clock_Ip_Init(...);  Port_Ci_Port_Ip_Init(...);  Dma_Ip_Init(...);   /* chỉ khi dùng DMA */
IntCtrl_Ip_Init(...); IntCtrl_Ip_ConfigIrqRouting(...);  OsIf_Init(...);
SpiDrv_Init(&SpiDrv_Config);          /* SpiDrv không khởi tạo lại các framework trên */
```

## 4. Dùng nhanh
```c
SpiDrv_SyncTransfer(dev, tx, rx, len, 1000U /*us/frame*/);
SpiDrv_AsyncTransfer(dev, tx, rx, len, &Cb, NULL_PTR);   /* ngắt/DMA: callback; polling: gọi SpiDrv_MainFunction() */
SpiDrv_SetBaud(dev, 500000U, &actual);                    /* chỉ thiết bị có SPIDRV_OVR_BUS, unit không BUSY */
```
`SPIDRV_OVR_*` quyết định trường nào của SpiDrv ghi đè giá trị từ tool; `SPIDRV_OVR_NONE` = dùng nguyên giá trị tool.

## 5. Khác với tài liệu thiết kế trước (sau khi đối chiếu code RTD)
| Thiết kế | Code thực tế | Lý do |
|---|---|---|
| `SPIDRV_MODE_DMA` | Chỉ `POLLING` / `INTERRUPT` | RTD quyết định DMA bằng `Lpspi_Ip_ConfigType.DmaUsed` (tool); `TransferMode` chỉ có 2 giá trị |
| Bảng hàm `SpiDrv_BackendOps` | Hàm `SpiDrv_Be_*` chọn lúc link | Đơn giản hơn; thay backend = thay file `.c` |
| `TX_UNDERRUN` / `RX_OVERRUN` | `SPIDRV_FIFO_ERROR` | IP chỉ báo chung `LPSPI_IP_FIFO_ERROR` |
| Platform dùng OsIf | SpiDrv không gọi OsIf | Timeout do `Lpspi_Ip_SyncTransmit` xử lý (qua OsIf bên trong IP) |
| FAULT chỉ thoát bằng ClearFault | Transfer mới vẫn được nhận khi FAULT; `SpiDrv_ClearFault` để xoá cờ | IP chỉ từ chối khi BUSY |

## 6. Đã kiểm tra và chưa kiểm tra
Đã làm: biên dịch sạch `-Wall -Wextra -Wconversion -Wshadow` với header RTD thật (`Lpspi_Ip.h`, `Lpspi_Ip_Types.h`, `Lpspi_Ip_Cfg.h` và PBcfg sinh ra), các header còn lại là stub; unit test host của `SpiDrv_Baud` (12 case) đạt.
Chưa làm: build với toolchain và RTD đầy đủ trong S32DS; chạy trên board.

## 7. Cần kiểm tra khi chạy thật
1. `Clock_Ip_GetClockFrequency(LPSPIn_CLK)` phải trả tần số functional clock (khác 0). Nếu trả 0, `SpiDrv_Init` báo `SPIDRV_CLOCK_ERROR`.
2. `SchM_Enter/Exit_Spi_SPI_EXCLUSIVE_AREA_00` phải được hiện thực trong `SchM_Spi.c` của RTD (nếu khu vực này đã dùng chỗ khác, đổi macro trong `SpiDrv_Cfg.h`).
3. `LPSPI_IP_TIMEOUT_METHOD` trong `Lpspi_Ip_Cfg.h` đang là `OSIF_COUNTER_DUMMY` ở example: timeout của `SpiDrv_SyncTransfer` chỉ có tác dụng thật khi OsIf dùng counter hệ thống.
4. Frame size hợp lệ theo IP: 4…32 bit; độ dài truyền tính bằng byte.
5. `SpiDrv_SetBaud` cần thiết bị khai báo `SPIDRV_OVR_BUS`; ở chế độ slave `BaudHz` là SCK tối đa của master (chỉ kiểm tra f_func ≥ 2×SCK).
6. Buffer dùng DMA đặt trong section `..._NO_CACHEABLE` qua `Spi_MemMap.h` như example.
