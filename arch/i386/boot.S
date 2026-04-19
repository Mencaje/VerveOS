.set MB2_MAGIC, 0x36d76289

.section .multiboot_header, "a"
.align 8
multiboot2_header_start:
    .long 0xe85250d6
    .long 0
    .long multiboot2_header_end - multiboot2_header_start
    .long -(0xe85250d6 + 0 + (multiboot2_header_end - multiboot2_header_start))

.align 8
info_req_start:
    .word 1
    .word 0
    .long info_req_end - info_req_start
    .long 6
    .long 4
info_req_end:

.align 8
    .word 0
    .word 0
    .long 8
multiboot2_header_end:

.section .bss
.align 16
stack_bottom:
.skip 32768
stack_top:

.section .text
.global _start
.type _start, @function

_start:
    cli
    movl $stack_top, %esp

    cmpl $MB2_MAGIC, %eax
    jne .Lbad_magic

    pushl %ebx
    pushl %eax
    call kernel_main

.Lhang:
    hlt
    jmp .Lhang

.Lbad_magic:
    jmp .Lhang

.size _start, . - _start
