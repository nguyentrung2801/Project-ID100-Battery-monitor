param(
    [string[]]$Envs = @("jig1", "jig2", "jig3"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $projectRoot "release"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
$python = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$bootApp0 = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"

if (!(Test-Path $pio)) { throw "PlatformIO pio.exe not found: $pio" }
if (!(Test-Path $python)) { throw "PlatformIO python.exe not found: $python" }
if (!(Test-Path $esptool)) { throw "esptool.py not found: $esptool" }
if (!(Test-Path $bootApp0)) { throw "boot_app0.bin not found: $bootApp0" }

function Get-JigLabel([string]$envName) {
    switch ($envName) {
        "jig1" { return "JIG1" }
        "jig2" { return "JIG2" }
        "jig3" { return "JIG3" }
        default { return $envName.ToUpperInvariant() }
    }
}

foreach ($envName in $Envs) {
    Write-Host "==== Building $envName ====" -ForegroundColor Cyan

    if (!$SkipBuild) {
        & $pio run -e $envName -j1
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $envName" }
    }

    $buildDir = Join-Path $projectRoot ".pio\build\$envName"
    $bootloader = Join-Path $buildDir "bootloader.bin"
    $partitions = Join-Path $buildDir "partitions.bin"
    $firmware = Join-Path $buildDir "firmware.bin"

    foreach ($file in @($bootloader, $partitions, $firmware)) {
        if (!(Test-Path $file)) { throw "Missing build output: $file" }
    }

    $label = Get-JigLabel $envName
    $merged = Join-Path $releaseDir "$label`_merged.bin"

    Write-Host "==== Merging $label firmware ====" -ForegroundColor Cyan
    & $python $esptool --chip esp32c3 merge_bin `
        -o $merged `
        --flash_mode dio `
        --flash_freq 80m `
        --flash_size 4MB `
        0x0 $bootloader `
        0x8000 $partitions `
        0xe000 $bootApp0 `
        0x10000 $firmware

    if ($LASTEXITCODE -ne 0) { throw "Merge failed for $envName" }

    $flashBat = Join-Path $releaseDir "flash_$($label.ToLowerInvariant()).bat"
    @"
@echo off
set PORT=%~1
if "%PORT%"=="" set /p PORT=Nhap cong COM, vi du COM3: 
if "%PORT%"=="" exit /b 1

where esptool.exe >nul 2>nul
if errorlevel 1 (
  echo Khong tim thay esptool.exe trong PATH.
  echo Cach de hon: dung Espressif Flash Download Tool va chon file $label`_merged.bin tai dia chi 0x0.
  pause
  exit /b 1
)

esptool.exe --chip esp32c3 --port %PORT% --baud 460800 write_flash 0x0 "%~dp0$label`_merged.bin"
pause
"@ | Set-Content -Path $flashBat -Encoding ASCII
}

$readme = Join-Path $releaseDir "README_FLASH.txt"
@"
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
"@ | Set-Content -Path $readme -Encoding ASCII

Write-Host "Release package created at: $releaseDir" -ForegroundColor Green
