.set CODE_SEL, 0x08
.set DATA_SEL, 0x10
.set VERVE_SMP_MAIL_PHYS, 0x9000

.section .text

.code16

.global smp_trampoline_start

smp_trampoline_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    addr32 lgdt gdtdesc

    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    .byte 0x66
    .byte 0xEA
    .long protected_entry
    .word CODE_SEL

.code32

protected_entry:
    movw $DATA_SEL, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movl $VERVE_SMP_MAIL_PHYS, %ebx
    movl (%ebx), %eax
    movl %eax, %cr3
    movl 4(%ebx), %esp
    movl 8(%ebx), %eax
    jmp *%eax

.align 16

gdt_null:
    .quad 0
gdt_code:
    .quad 0x00CF9A000000FFFF
gdt_data:
    .quad 0x00CF92000000FFFF

.align 16

gdtdesc:
    .word gdt_end - gdt_null - 1
    .long gdt_null

gdt_end:

.size smp_trampoline_start, . - smp_trampoline_start
