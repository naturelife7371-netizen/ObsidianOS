; ============================================================================
;  ObsidianOS - 32-bit kernel entry
;
;  The bootloader already enabled the A20 line, loaded a temporary flat GDT
;  and switched the CPU to protected mode before jumping to 0x00010000.
;  This file installs the kernel's own GDT, reloads every segment register,
;  sets up a stack and calls kernel_main() in kernel.c.
; ============================================================================

[BITS 32]

; MinGW/PE prefixes C symbols with an underscore, ELF does not.
%ifidn __OUTPUT_FORMAT__, win32
    extern _kernel_main
    %define KMAIN _kernel_main
%else
    extern kernel_main
    extern _bss_start
    extern _bss_end
    %define KMAIN kernel_main
%endif

global _start

section .text
_start:
    cli
    cld

    ; Install the kernel GDT (flat 4GB code/data segments).
    lgdt [gdtr32]

    ; Reload CS with the new code selector.
    jmp 0x08:.flush
.flush:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Stack: 0x00090000 is safely above the kernel image and below the EBDA.
    mov esp, 0x00090000
    mov ebp, esp

    ; Zero the .bss section (the flat image does not contain it).
%ifnidn __OUTPUT_FORMAT__, win32
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
%endif

    call KMAIN

.hang:
    cli
    hlt
    jmp .hang

; ----------------------------------------------------------------------------
align 8
gdt32_start:
    dq 0x0000000000000000           ; null
    dw 0xFFFF                       ; code: base 0, limit 4GB, 32-bit, ring 0
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00
    dw 0xFFFF                       ; data: base 0, limit 4GB, 32-bit, ring 0
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00
gdt32_end:

gdtr32:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start
