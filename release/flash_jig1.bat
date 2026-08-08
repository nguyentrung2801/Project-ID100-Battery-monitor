@echo off
set PORT=%~1
if "%PORT%"=="" set /p PORT=Nhap cong COM, vi du COM3: 
if "%PORT%"=="" exit /b 1

where esptool.exe >nul 2>nul
if errorlevel 1 (
  echo Khong tim thay esptool.exe trong PATH.
  echo Cach de hon: dung Espressif Flash Download Tool va chon file JIG1_merged.bin tai dia chi 0x0.
  pause
  exit /b 1
)

esptool.exe --chip esp32c3 --port %PORT% --baud 460800 write_flash 0x0 "%~dp0JIG1_merged.bin"
pause
