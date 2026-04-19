/*
 * Multiboot2 (32-bit entry) → PAE → long mode stub (long64_stub) → inner ELF64 blob.
 * Linked as ELF32 i386 so GRUB / QEMU recognize the multiboot2 header.
 */
.set MB2_MAGIC, 0x36d76289
.set COM1_DATA, 0x3F8
.set COM1_LSR, 0x3FD
.set LSR_THRE, 0x20

.section .multiboot_header, "a"
.align 8
multiboot2_header_start:
    .long 0xe85250d6
    .long 0
    .long multiboot2_header_end - multiboot2_header_start
    .long 0x17adaec0

.align 8
info_req_start:
    .long 1
    .long 0
    .long info_req_end - info_req_start
    .long 6
    .long 4
    .long 15
info_req_end:

.align 8
    .long 0
    .long 8
multiboot2_header_end:

.section .bss
.align 16
stack32_bottom:
.skip 32768
stack32_top:

.align 16
stack64_bottom:
.skip 32768
stack64_top:

.align 4096
boot_pml4:
.skip 4096
boot_pdpt:
.skip 4096
boot_pd:
.skip 4096

.section .saved_mb2, "aw"
.align 4
saved_magic:
.long 0
saved_mb_phys:
.long 0

.section .data
.align 16
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt64_end:

.align 4
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.section .text
.code32
.global _start
.type _start, @function

.extern long64_entry

_start:
    cli
    movl $stack32_top, %esp

    movb $'1', %al
    call com1_putc_32
    call com1_crlf_32

    cmpl $MB2_MAGIC, %eax
    jne .Lbad_magic

    movb $'2', %al
    call com1_putc_32

    movl %eax, saved_magic
    movl %ebx, saved_mb_phys

    movb $'3', %al
    call com1_putc_32

    call setup_identity_map_pg

    movb $'4', %al
    call com1_putc_32

    call enable_long_mode_prep

.Lhang32:
    hlt
    jmp .Lhang32

.Lbad_magic:
    movb $'!', %al
    call com1_putc_32
    jmp .Lhang32

com1_tx_wait_32:
    movl $COM1_LSR, %edx
0:
    inb %dx, %al
    testb $LSR_THRE, %al
    jz 0b
    ret

com1_putc_32:
    pushl %eax
    call com1_tx_wait_32
    popl %eax
    movl $COM1_DATA, %edx
    outb %al, %dx
    ret

com1_crlf_32:
    movb $13, %al
    call com1_putc_32
    movb $10, %al
    jmp com1_putc_32

setup_identity_map_pg:
    pushl %edi
    pushl %ecx
    pushl %eax

    movl $boot_pd, %edi
    xorl %ecx, %ecx
.Lpd:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $8, %edi
    incl %ecx
    cmpl $512, %ecx
    jl .Lpd

    movl $boot_pdpt, %eax
    orl $0x03, %eax
    movl %eax, boot_pml4 + 0
    movl $0, boot_pml4 + 4

    movl $boot_pd, %eax
    orl $0x03, %eax
    movl %eax, boot_pdpt + 0
    movl $0, boot_pdpt + 4

    popl %eax
    popl %ecx
    popl %edi
    ret

.set IA32_EFER, 0xC0000080
.set EFER_LME, 0x100

enable_long_mode_prep:
    movb $'5', %al
    call com1_putc_32

    lgdt gdt64_ptr

    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    movl $boot_pml4, %eax
    movl %eax, %cr3

    movl $IA32_EFER, %ecx
    rdmsr
    orl $EFER_LME, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    ljmpl $8, $long64_entry
