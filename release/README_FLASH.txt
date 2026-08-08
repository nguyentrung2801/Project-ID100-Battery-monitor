GDO / SMO Fault Monitor firmware release
=======================================

Cac file *_merged.bin la firmware da gom san bootloader + partition + app.
Nguoi nap firmware KHONG can VSCode va KHONG can PlatformIO.

Cach nap khuyen dung: Espressif Flash Download Tool
--------------------------------------------------
1. Tai va mo Espressif Flash Download Tool tren Windows.
2. Chon chip: ESP32-C3.
3. Chon che do download/develop tuy tool hien thi.
4. Them file can nap:
   - JIG1_merged.bin cho JIG 1
   - JIG2_merged.bin cho JIG 2
   - JIG3_merged.bin cho JIG 3
5. Dia chi flash/offset: 0x0
6. Chon dung COM port cua ESP.
7. Baud co the de 460800 hoac 115200 neu nap khong on dinh.
8. Bam START/FLASH.
9. Sau khi nap xong, reset ESP.

Cach nap bang command line neu da cai esptool.exe
------------------------------------------------
flash_jig1.bat COM3
flash_jig2.bat COM3
flash_jig3.bat COM3

Neu loi khong mo duoc COM
-------------------------
- Dong Serial Monitor / PlatformIO / UART Assistant dang giu cong COM.
- Rut cam lai ESP.
- Kiem tra dung cong COM trong Device Manager.
- Neu can, giu nut BOOT khi bat dau nap, sau do tha ra khi tool bat dau ghi.

Ghi chu
-------
- Flash address cho file merged: 0x0
- Chip target: ESP32-C3
- Neu can xoa WiFi/log cu, trong Flash Download Tool co the tick ERASE truoc khi nap.
