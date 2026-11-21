; ============================================================================
;  ObsidianOS - Stage 1 bootloader (512 byte MBR / floppy boot sector)
;
;  NASM flat binary, ORG 0x7C00, 16-bit real mode.
;
;  1. Prints a banner through the BIOS teletype service.
;  2. Loads the kernel (entry.asm + kernel.c + pci.c + rtl8139.c) from
;     LBA 1 into physical address 0x00010000 (segment 0x1000:0x0000).
;     It first tries the INT 13h LBA extensions (AH=42h) and falls back
;     to classic CHS reads with 1.44MB floppy geometry (18/2/80).
;  3. Enables the A20 line, loads a flat GDT and switches the CPU to
;     32-bit protected mode with a far jump to 0x0008:0x00010000.
; ============================================================================

[BITS 16]
[ORG 0x7C00]

KERNEL_SEG      equ 0x1000          ; 0x1000:0x0000 = physical 0x10000
KERNEL_LBA      equ 1               ; kernel starts right after the boot sector
KERNEL_SECTORS  equ 120             ; 120 * 512 = 61440 bytes of kernel space

; ----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl            ; BIOS passes the boot drive in DL

    mov si, msg_banner
    call print_string

    ; ------------------------------------------------------------------
    ; Try INT 13h extended read (LBA). This works on hard disks and on
    ; every reasonably modern BIOS, including QEMU/SeaBIOS floppies.
    ; ------------------------------------------------------------------
    mov word [dap], 0x0010          ; packet size (16 bytes)
    mov word [dap + 2], KERNEL_SECTORS
    mov word [dap + 4], 0x0000      ; destination offset
    mov word [dap + 6], KERNEL_SEG  ; destination segment
    mov dword [dap + 8], KERNEL_LBA
    mov dword [dap + 12], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jnc .loaded

    ; ------------------------------------------------------------------
    ; Fallback: CHS reads, one sector at a time (1.44MB floppy geometry).
    ; ------------------------------------------------------------------
    mov si, msg_chs
    call print_string

    mov word [sectors_left], KERNEL_SECTORS
    mov word [cur_lba], KERNEL_LBA
    mov word [load_seg], KERNEL_SEG
    mov word [load_off], 0

.read_loop:
    ; LBA -> cylinder / head / sector
    mov ax, [cur_lba]
    xor dx, dx
    mov cx, 18
    div cx                          ; ax = lba / 18, dx = lba % 18
    inc dl
    mov [chs_sector], dl
    xor dx, dx
    mov cx, 2
    div cx                          ; ax = cylinder, dx = head
    mov [chs_cyl], al
    mov [chs_head], dl

    mov ax, [load_seg]
    mov es, ax
    mov bx, [load_off]

    mov ch, [chs_cyl]
    mov cl, [chs_sector]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    mov ax, 0x0201                  ; AH=02 read, AL=1 sector
    int 0x13
    jc .disk_error

    add word [load_off], 512
    inc word [cur_lba]
    dec word [sectors_left]
    jnz .read_loop

.loaded:
    mov si, msg_loaded
    call print_string

    ; ------------------------------------------------------------------
    ; Enable A20 through the "fast A20" port 0x92
    ; ------------------------------------------------------------------
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al

    ; ------------------------------------------------------------------
    ; Load GDT and enter 32-bit protected mode
    ; ------------------------------------------------------------------
    cli
    lgdt [gdtr]

    mov eax, cr0
    or eax, 0x00000001              ; CR0.PE = 1
    mov cr0, eax

    ; 66 EA <offset32> <selector16> : far jump with a 32-bit offset
    db 0x66, 0xEA
    dd 0x00010000
    dw 0x0008

.disk_error:
    mov si, msg_error
    call print_string
.halt:
    cli
    hlt
    jmp .halt

; ----------------------------------------------------------------------------
; print_string - SI = NUL terminated string, BIOS teletype output
; ----------------------------------------------------------------------------
print_string:
    push ax
    push bx
    push si
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
.done:
    pop si
    pop bx
    pop ax
    ret

; ----------------------------------------------------------------------------
; Data
; ----------------------------------------------------------------------------
boot_drive:     db 0
sectors_left:   dw 0
cur_lba:        dw 0
load_seg:       dw 0
load_off:       dw 0
chs_sector:     db 0
chs_cyl:        db 0
chs_head:       db 0

msg_banner:     db "ObsidianOS bootloader", 13, 10, 0
msg_chs:        db "LBA read failed, falling back to CHS...", 13, 10, 0
msg_loaded:     db "Kernel loaded, entering protected mode...", 13, 10, 0
msg_error:      db "DISK READ ERROR - system halted", 13, 10, 0

align 4
dap:
    dw 0x0010                       ; size of packet
    dw 0                            ; number of sectors
    dw 0                            ; buffer offset
    dw 0                            ; buffer segment
    dq 0                            ; starting LBA

align 8
gdt_start:
    dq 0x0000000000000000           ; null descriptor
gdt_code:                           ; 0x08: flat 32-bit code, ring 0
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00
gdt_data:                           ; 0x10: flat 32-bit data, ring 0
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00
gdt_end:
gdtr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
