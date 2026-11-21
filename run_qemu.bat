@echo off
rem ===========================================================================
rem  ObsidianOS - run the disk image in QEMU
rem
rem  Boots build\ObsidianOS.img as a 1.44MB floppy and attaches an RTL8139
rem  Ethernet adapter backed by QEMU's user-mode (slirp) network:
rem      guest 10.0.2.15/24  ->  gateway 10.0.2.2
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"

set "IMG=%~dp0build\ObsidianOS.img"

if not exist "%IMG%" (
    echo [ERROR] %IMG% was not found.
    echo         Run build.bat first.
    echo.
    pause
    exit /b 1
)

rem ---------------------------------------------------------------------------
rem  Locate QEMU
rem ---------------------------------------------------------------------------
set "QEMU="
for %%P in (qemu-system-i386.exe) do if not defined QEMU set "QEMU=%%~$PATH:P"
if not defined QEMU for %%P in (qemu-system-x86_64.exe) do if not defined QEMU set "QEMU=%%~$PATH:P"

if not defined QEMU if exist "%ProgramFiles%\qemu\qemu-system-i386.exe" set "QEMU=%ProgramFiles%\qemu\qemu-system-i386.exe"
if not defined QEMU if exist "%ProgramFiles%\qemu\qemu-system-x86_64.exe" set "QEMU=%ProgramFiles%\qemu\qemu-system-x86_64.exe"
if not defined QEMU if exist "%ProgramFiles(x86)%\qemu\qemu-system-i386.exe" set "QEMU=%ProgramFiles(x86)%\qemu\qemu-system-i386.exe"
if not defined QEMU if exist "%LOCALAPPDATA%\Programs\qemu\qemu-system-i386.exe" set "QEMU=%LOCALAPPDATA%\Programs\qemu\qemu-system-i386.exe"
if not defined QEMU if exist "%LOCALAPPDATA%\ObsidianOS-tools\qemu\qemu-system-i386.exe" set "QEMU=%LOCALAPPDATA%\ObsidianOS-tools\qemu\qemu-system-i386.exe"
if not defined QEMU if exist "C:\qemu\qemu-system-i386.exe" set "QEMU=C:\qemu\qemu-system-i386.exe"
if not defined QEMU if exist "%USERPROFILE%\scoop\shims\qemu-system-i386.exe" set "QEMU=%USERPROFILE%\scoop\shims\qemu-system-i386.exe"
if not defined QEMU if exist "%ProgramData%\chocolatey\bin\qemu-system-i386.exe" set "QEMU=%ProgramData%\chocolatey\bin\qemu-system-i386.exe"
if not defined QEMU if exist "%LOCALAPPDATA%\ObsidianOS-tools\qemu" for /r "%LOCALAPPDATA%\ObsidianOS-tools\qemu" %%Q in (qemu-system-i386.exe) do if not defined QEMU set "QEMU=%%Q"
if not defined QEMU if exist "%LOCALAPPDATA%\ObsidianOS-tools\qemu" for /r "%LOCALAPPDATA%\ObsidianOS-tools\qemu" %%Q in (qemu-system-x86_64.exe) do if not defined QEMU set "QEMU=%%Q"

if not defined QEMU (
    echo [ERROR] QEMU was not found.
    echo.
    echo   Install it with:
    echo       winget install --id SoftwareFreedomConservancy.QEMU -e
    echo   or download it from https://www.qemu.org/download/#windows
    echo   and add qemu-system-i386.exe to PATH.
    echo.
    pause
    exit /b 1
)

echo Starting ObsidianOS in QEMU...
echo   QEMU  : %QEMU%
echo   Image : %IMG%
echo   NIC   : rtl8139 on QEMU user-mode network
echo.

"%QEMU%" ^
    -name "ObsidianOS" ^
    -m 64 ^
    -boot a ^
    -drive file="%IMG%",format=raw,if=floppy ^
    -netdev user,id=net0 ^
    -device rtl8139,netdev=net0 ^
    %*

if errorlevel 1 (
    echo.
    echo [ERROR] QEMU exited with an error.
    pause
)

exit /b 0
