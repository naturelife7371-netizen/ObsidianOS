# ObsidianOS

Minimalist custom operating system built from scratch with Assembly and C, featuring a command-line interface (CLI), PCI bus scanning, and RTL8139 Ethernet driver experiments for x86 and QEMU.

**Version:** `0.2.0` (Installer + ISO + Mouse support)  
**License:** GNU General Public License v3.0 (GPLv3)

---

## Overview

ObsidianOS is a lightweight, bare-metal operating system prototype developed on Windows using **NASM**, a C compiler (**MinGW / GCC / Clang**), and **QEMU**. The project focuses on:

- Booting a custom 16-bit bootloader (`boot.asm`) and transitioning toward a 32-bit C kernel (`entry.asm` & `kernel.c`).
- Providing a minimal text-mode command prompt (`help`, `cls`, `reboot`, and upcoming `pci` / `net` commands).
- Exploring PCI device discovery (`pci.c`) and RTL8139 network card driver integration (`rtl8139.c`) for future server and network connectivity.

---

## Project Structure

```text
ObsidianOS/
├── src/
│   ├── boot/
│   │   └── boot.asm      # Bootloader and initial shell prototype
│   ├── kernel/
│   │   ├── entry.asm     # 32-bit protected mode entry and kernel jump
│   │   └── kernel.c      # Core kernel logic, VGA text output, and commands
│   └── net/
│       ├── pci.c         # PCI bus scanning for hardware/NIC detection
│       └── rtl8139.c     # RTL8139 Ethernet driver foundation
├── build/                # Generated build artifacts (.bin, .o, .iso)
├── build.bat             # Build script for Windows
├── run_qemu.bat          # QEMU launch script with virtual networking
├── LICENSE               # GPLv3 license
└── README.md
