@echo off
rem ===========================================================================
rem  ObsidianOS - build script
rem
rem  Compiles the bootloader and the freestanding kernel, links them into a
rem  flat 32-bit image and packs everything into a bootable 1.44MB floppy
rem  image at build\ObsidianOS.img.
rem
rem  Supported toolchains (first one found wins):
rem    1. i686-elf-gcc  / i386-elf-gcc  (recommended cross compiler)
rem    2. clang + ld.lld + llvm-objcopy (LLVM, no sysroot needed)
rem    3. i686-w64-mingw32-gcc / plain MinGW gcc with -m32 support
rem ===========================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "SRC=%ROOT%src"

echo ============================================================
echo   ObsidianOS build
echo ============================================================
echo.

if not exist "%BUILD%" mkdir "%BUILD%"

rem ---------------------------------------------------------------------------
rem  Source sanity check
rem ---------------------------------------------------------------------------
set "MISSING=0"
for %%F in (
    "%SRC%\boot\boot.asm"
    "%SRC%\kernel\entry.asm"
    "%SRC%\kernel\kernel.c"
    "%SRC%\net\pci.c"
    "%SRC%\net\rtl8139.c"
) do (
    if not exist "%%~F" (
        echo [ERROR] Missing source file: %%~F
        set "MISSING=1"
    )
)
if "%MISSING%"=="1" goto :fail

rem ---------------------------------------------------------------------------
rem  Locate NASM
rem ---------------------------------------------------------------------------
set "NASM="
for %%P in (nasm.exe) do if not defined NASM set "NASM=%%~$PATH:P"

if not defined NASM if exist "%ProgramFiles%\NASM\nasm.exe" set "NASM=%ProgramFiles%\NASM\nasm.exe"
if not defined NASM if exist "%ProgramFiles(x86)%\NASM\nasm.exe" set "NASM=%ProgramFiles(x86)%\NASM\nasm.exe"
if not defined NASM if exist "%LOCALAPPDATA%\NASM\nasm.exe" set "NASM=%LOCALAPPDATA%\NASM\nasm.exe"
if not defined NASM if exist "%LOCALAPPDATA%\bin\NASM\nasm.exe" set "NASM=%LOCALAPPDATA%\bin\NASM\nasm.exe"
if not defined NASM if exist "C:\nasm\nasm.exe" set "NASM=C:\nasm\nasm.exe"
if not defined NASM if exist "%USERPROFILE%\scoop\shims\nasm.exe" set "NASM=%USERPROFILE%\scoop\shims\nasm.exe"
if not defined NASM if exist "%ProgramData%\chocolatey\bin\nasm.exe" set "NASM=%ProgramData%\chocolatey\bin\nasm.exe"
if not defined NASM for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\NASM.NASM_*") do (
    if not defined NASM if exist "%%~D\nasm.exe" set "NASM=%%~D\nasm.exe"
)

if not defined NASM (
    echo [ERROR] NASM was not found.
    echo.
    echo   Install it with:
    echo       winget install --id NASM.NASM -e
    echo   or download it from https://www.nasm.us and add nasm.exe to PATH.
    goto :fail
)

rem ---------------------------------------------------------------------------
rem  Locate a C toolchain / linker / objcopy
rem ---------------------------------------------------------------------------
rem A few common places where people unpack cross toolchains.
if exist "C:\i686-elf\bin" set "PATH=C:\i686-elf\bin;%PATH%"
if exist "%USERPROFILE%\i686-elf\bin" set "PATH=%USERPROFILE%\i686-elf\bin;%PATH%"
if exist "C:\cross\bin" set "PATH=C:\cross\bin;%PATH%"
if exist "%ProgramFiles%\LLVM\bin\clang.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
if exist "%LOCALAPPDATA%\Programs\LLVM\bin\clang.exe" set "PATH=%LOCALAPPDATA%\Programs\LLVM\bin;%PATH%"
if exist "%LOCALAPPDATA%\ObsidianOS-tools\llvm\bin\clang.exe" set "PATH=%LOCALAPPDATA%\ObsidianOS-tools\llvm\bin;%PATH%"

set "CC="
set "LD="
set "OBJCOPY="
set "LINKMODE="
set "CFLAGS_EXTRA="
set "TOOLCHAIN="

call :try_prefix i686-elf- elf
if not defined CC call :try_prefix i386-elf- elf
if not defined CC call :try_clang
if not defined CC call :try_prefix i686-w64-mingw32- pe
if not defined CC call :try_prefix i386-w64-mingw32- pe
if not defined CC call :try_plain_gcc

if not defined CC (
    echo [ERROR] No usable 32-bit C toolchain was found.
    echo.
    echo   Tried: i686-elf-gcc, i386-elf-gcc, clang+ld.lld,
    echo          i686-w64-mingw32-gcc, i386-w64-mingw32-gcc, gcc -m32
    echo.
    echo   Recommended (single install, no sysroot needed^):
    echo       winget install --id LLVM.LLVM -e
    echo   Then run build.bat again.
    echo.
    echo   Alternative: an i686-elf cross compiler
    echo       https://wiki.osdev.org/GCC_Cross-Compiler
    goto :fail
)

echo Toolchain : %TOOLCHAIN%
echo NASM      : %NASM%
echo Link mode : %LINKMODE%
echo.

rem ---------------------------------------------------------------------------
rem  Compiler flags
rem ---------------------------------------------------------------------------
rem  -mno-sse/-mno-mmx keep clang/gcc from auto-vectorising memcpy/memset with
rem  SSE instructions: the kernel never enables CR4.OSFXSR, so any SSE opcode
rem  would fault (#GP/#UD) at boot.
set "CFLAGS=-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-exceptions -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-mmx -mno-avx -mno-avx2 -O2 -Wall -Wextra"
if defined CFLAGS_EXTRA (
    set "CFLAGS=%CFLAGS% %CFLAGS_EXTRA%"
) else (
    set "CFLAGS=%CFLAGS% -m32"
)
if "%LINKMODE%"=="pe" set "CFLAGS=%CFLAGS% -mno-stack-arg-probe"

rem ---------------------------------------------------------------------------
rem  Compile C sources
rem ---------------------------------------------------------------------------
echo [1/4] Compiling kernel.c ...
"%CC%" %CFLAGS% -c "%SRC%\kernel\kernel.c" -o "%BUILD%\kernel.o"
if errorlevel 1 goto :compile_fail

echo [2/4] Compiling pci.c and rtl8139.c ...
"%CC%" %CFLAGS% -c "%SRC%\net\pci.c" -o "%BUILD%\pci.o"
if errorlevel 1 goto :compile_fail

"%CC%" %CFLAGS% -c "%SRC%\net\rtl8139.c" -o "%BUILD%\rtl8139.o"
if errorlevel 1 goto :compile_fail

rem ---------------------------------------------------------------------------
rem  Assemble
rem ---------------------------------------------------------------------------
if "%LINKMODE%"=="pe" (
    set "NASMFMT=win32"
) else (
    set "NASMFMT=elf32"
)

echo [3/4] Assembling boot.asm (NASM) and entry.S (clang) ...
"%NASM%" -f bin "%SRC%\boot\boot.asm" -o "%BUILD%\boot.bin"
if errorlevel 1 goto :compile_fail

"%CC%" --target=x86_64-elf -c "%SRC%\kernel\entry.S" -o "%BUILD%\entry.o"
if errorlevel 1 goto :compile_fail

rem ---------------------------------------------------------------------------
rem  Link
rem ---------------------------------------------------------------------------
echo [4/4] Linking kernel and creating the disk image ...

if "%LINKMODE%"=="elf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$lines=@('ENTRY(_start)','SECTIONS','{','  . = 0x10000;','  .text : { *(.text.entry) *(.text .text.*) }','  .rodata : { *(.rodata .rodata.*) }','  .data : { *(.data .data.*) }','  _bss_start = .;','  .bss : { *(COMMON) *(.bss .bss.*) }','  _bss_end = .;','  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }','}'); Set-Content -Path '%BUILD%\linker.ld' -Value $lines -Encoding ASCII"
    if errorlevel 1 goto :link_fail

    "%LD%" -m elf_x86_64 -T "%BUILD%\linker.ld" -o "%BUILD%\kernel.elf" "%BUILD%\entry.o" "%BUILD%\kernel.o" "%BUILD%\pci.o" "%BUILD%\rtl8139.o"
    if errorlevel 1 goto :link_fail

    "%OBJCOPY%" -O binary "%BUILD%\kernel.elf" "%BUILD%\kernel.bin"
    if errorlevel 1 goto :link_fail
) else (
    "%LD%" -m i386pe -e _start -Ttext 0x10000 --oformat binary -o "%BUILD%\kernel.bin" "%BUILD%\entry.o" "%BUILD%\kernel.o" "%BUILD%\pci.o" "%BUILD%\rtl8139.o"
    if errorlevel 1 goto :link_fail
)

rem ---------------------------------------------------------------------------
rem  Build the 1.44MB floppy image: boot sector at LBA 0, kernel at LBA 1
rem ---------------------------------------------------------------------------
powershell -NoProfile -ExecutionPolicy Bypass -Command "$boot=[IO.File]::ReadAllBytes('%BUILD%\boot.bin'); $kernel=[IO.File]::ReadAllBytes('%BUILD%\kernel.bin'); if ($boot.Length -ne 512) { Write-Host '[ERROR] boot.bin is not 512 bytes'; exit 1 }; if ($kernel.Length -gt 61440) { Write-Host ('[ERROR] kernel.bin is too large: ' + $kernel.Length + ' bytes (max 61440)'); exit 1 }; $image=New-Object byte[] 1474560; [Array]::Copy($boot,0,$image,0,512); [Array]::Copy($kernel,0,$image,512,$kernel.Length); [IO.File]::WriteAllBytes('%BUILD%\ObsidianOS.img',$image); Write-Host ('  boot sector : ' + $boot.Length + ' bytes'); Write-Host ('  kernel      : ' + $kernel.Length + ' bytes'); Write-Host ('  disk image  : ' + $image.Length + ' bytes')"
if errorlevel 1 goto :link_fail

echo.
echo ============================================================
rem ---------------------------------------------------------------------------
rem  Optional: build a bootable ISO (El Torito) if Python + pycdlib exist
rem ---------------------------------------------------------------------------
where python >nul 2>nul
if not errorlevel 1 (
    python "%ROOT%make_iso.py" >nul 2>nul
    if not errorlevel 1 (
        echo   ISO     : %BUILD%\ObsidianOS.iso
    )
)

echo   BUILD OK
echo   Image: %BUILD%\ObsidianOS.img
echo   Run  : run_qemu.bat
echo ============================================================
echo.
exit /b 0

rem ---------------------------------------------------------------------------
rem  Helper subroutines
rem ---------------------------------------------------------------------------
:try_prefix
rem %1 = tool prefix (e.g. i686-elf-), %2 = link mode (elf / pe)
set "PFX=%~1"
set "MODE=%~2"
where %PFX%gcc >nul 2>nul
if errorlevel 1 goto :eof
where %PFX%ld >nul 2>nul
if errorlevel 1 goto :eof
where %PFX%objcopy >nul 2>nul
if errorlevel 1 goto :eof
set "CC=%PFX%gcc"
set "LD=%PFX%ld"
set "OBJCOPY=%PFX%objcopy"
set "LINKMODE=%MODE%"
set "TOOLCHAIN=%PFX%gcc (%MODE)"
goto :eof

:try_clang
where clang >nul 2>nul
if errorlevel 1 goto :eof
where ld.lld >nul 2>nul
if errorlevel 1 goto :eof
where llvm-objcopy >nul 2>nul
if errorlevel 1 goto :eof
set "CC=clang"
set "LD=ld.lld"
set "OBJCOPY=llvm-objcopy"
set "LINKMODE=elf"
set "CFLAGS_EXTRA=--target=x86_64-elf"
set "TOOLCHAIN=clang --target=x86_64-elf + ld.lld"
goto :eof

:try_plain_gcc
where gcc >nul 2>nul
if errorlevel 1 goto :eof
where ld >nul 2>nul
if errorlevel 1 goto :eof
where objcopy >nul 2>nul
if errorlevel 1 goto :eof

rem Make sure this compiler can actually emit 32-bit code.
echo int main(void^){return 0;} > "%BUILD%\_probe.c"
gcc -m32 -c "%BUILD%\_probe.c" -o "%BUILD%\_probe.o" >nul 2>nul
if errorlevel 1 (
    del "%BUILD%\_probe.c" >nul 2>nul
    del "%BUILD%\_probe.o" >nul 2>nul
    echo [WARN] gcc was found but cannot produce 32-bit code with -m32.
    goto :eof
)
del "%BUILD%\_probe.c" >nul 2>nul
del "%BUILD%\_probe.o" >nul 2>nul

set "LDINFO="
for /f "delims=" %%L in ('ld -V 2^>^&1') do if not defined LDINFO set "LDINFO=%%L"
echo !LDINFO! | findstr /i "pe-i386" >nul
if not errorlevel 1 (
    set "LINKMODE=pe"
) else (
    set "LINKMODE=elf"
)
set "CC=gcc"
set "LD=ld"
set "OBJCOPY=objcopy"
set "TOOLCHAIN=gcc -m32 (%LINKMODE%)"
goto :eof

rem ---------------------------------------------------------------------------
rem  Error exits
rem ---------------------------------------------------------------------------
:compile_fail
echo.
echo [ERROR] Compilation or assembly failed. See the messages above.
goto :fail

:link_fail
echo.
echo [ERROR] Linking or image creation failed. See the messages above.
goto :fail

:fail
echo.
echo Build failed.
exit /b 1
