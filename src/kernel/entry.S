# ============================================================================
# ObsidianOS - entry point (32-bit -> 64-bit long mode)
# Assembled with clang --target=x86_64-elf
# ============================================================================
.code32

.global _start
_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp

    # Zero page tables at 0x70000-0x72FFF
    movl $0x70000, %edi
    movl $0xC00, %ecx
    xorl %eax, %eax
    rep stosl

    # PML4[0] = 0x71003
    movl $0x71003, %eax
    movl $0x70000, %ebx
    movl %eax, (%ebx)
    # PDPT[0] = 0x72003
    movl $0x72003, %eax
    movl $0x71000, %ebx
    movl %eax, (%ebx)

    # PD: 512 x 2MB huge pages
    movl $0x72000, %edi
    movl $0x83, %eax
    movl $0x200, %ecx
fill_pd:
    movl %eax, (%edi)
    addl $0x200000, %eax
    addl $0x8, %edi
    loop fill_pd

    # GDT at 0x73000
    movl $0x73000, %ebx
    movl $0, (%ebx)
    movl $0, 4(%ebx)
    movl $0x0000FFFF, 8(%ebx)
    movl $0x00CF9A00, 12(%ebx)
    movl $0x0000FFFF, 16(%ebx)
    movl $0x00CF9200, 20(%ebx)
    movl $0x00000000, 24(%ebx)
    movl $0x00209A00, 28(%ebx)
    movl $0x00000000, 32(%ebx)
    movl $0x00009200, 36(%ebx)

    # GDTR at 0x73030
    movl $0x73030, %ebx
    movw $0x27, (%ebx)
    movl $0x73000, 2(%ebx)
    movl $0, 6(%ebx)

    # Load GDT
    movl $0x73030, %ebx
    lgdt (%ebx)

    # CR3 = PML4
    movl $0x70000, %eax
    movl %eax, %cr3

    # Enable PAE
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    # Enable long mode (EFER.LME)
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    # Enable paging
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    # Far jump to 64-bit code
    pushl $0x18
    pushl $long_mode
    retl

.code64
long_mode:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movq $0x90000, %rsp

    # Zero .bss
    leaq _bss_start(%rip), %rdi
    leaq _bss_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    call kernel_main

halt:
    cli
    hlt
    jmp halt

# Externs
.extern _bss_start
.extern _bss_end
.extern kernel_main
