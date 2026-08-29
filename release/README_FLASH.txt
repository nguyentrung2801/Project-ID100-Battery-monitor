ID100 Battery Monitor firmware release
======================================

Recommended complete image
--------------------------
File: ID100_Battery_Monitor_merged.bin
Flash address: 0x0

This file contains the bootloader, partition table, boot_app0 and application.
Use it for a complete installation with Espressif Flash Download Tool or:

    flash_id100.bat COM3

Application-only image
----------------------
File: ID100_Battery_Monitor.bin
Flash address: 0x10000

Use the application-only image only when the board already has the matching
bootloader and partition table.

Target
------
Chip: ESP32-C3
Flash size: 4 MB
Flash mode: DIO
PlatformIO environment: esp32-c3-mini

Important
---------
Normal firmware flashing preserves NVS and LittleFS data. Erasing the whole
flash removes saved Wi-Fi, test information, channel states and telemetry buffer.
