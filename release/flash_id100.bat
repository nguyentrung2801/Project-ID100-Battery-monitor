@echo off
set PORT=%~1
if "%PORT%"=="" set /p PORT=Enter COM port, for example COM3: 
if "%PORT%"=="" exit /b 1

where esptool.exe >nul 2>nul
if errorlevel 1 (
  echo esptool.exe was not found in PATH.
  echo Use Espressif Flash Download Tool and flash ID100_Battery_Monitor_merged.bin at 0x0.
  pause
  exit /b 1
)

esptool.exe --chip esp32c3 --port %PORT% --baud 460800 write_flash 0x0 "%~dp0ID100_Battery_Monitor_merged.bin"
pause
