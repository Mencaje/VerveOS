.section .text
.global gdt_flush

gdt_flush:
    mov $0x10, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    ljmp $0x08, $flush
flush:
    movw $0x28, %ax
    ltr %ax
    ret
