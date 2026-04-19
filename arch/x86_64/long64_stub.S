/*
 * Runs in 64-bit mode after boot32 ljmpl. Uses fixed physical addresses for saved
 * multiboot fields (see boot_layout.inc / linker_amd64_boot.ld).
 */
/* Injected by -Wa,-I…/include when building this file (see Makefile). */
.include "arch/x86_64/boot_layout.inc"

.set COM1_DATA, 0x3F8
.set COM1_LSR, 0x3FD
.set LSR_THRE, 0x20

.section .text
.code64
.global long64_entry
.type long64_entry, @function

long64_entry:
    movabs $VERV_STACK64_TOP_PHYS, %rsp
    xorq %rbp, %rbp

    movb $'6', %al
    call com1_putc_64
    call com1_crlf_64

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movabs $VERV_SAVED_MAGIC_PHYS, %rax
    movl (%rax), %edi
    movabs $VERV_MB_INFO_PHYS, %rax
    movl (%rax), %esi

    xorq %rdx, %rdx
    xorq %rcx, %rcx
    xorq %r8, %r8
    xorq %r9, %r9

    movabs $VERV_INNER_PHYS, %rax
    call *%rax

.Lhang64:
    hlt
    jmp .Lhang64

com1_tx_wait_64:
    movl $COM1_LSR, %edx
0:
    inb %dx, %al
    testb $LSR_THRE, %al
    jz 0b
    ret

com1_putc_64:
    pushq %rax
    call com1_tx_wait_64
    popq %rax
    movl $COM1_DATA, %edx
    outb %al, %dx
    ret

com1_crlf_64:
    movb $13, %al
    call com1_putc_64
    movb $10, %al
    jmp com1_putc_64
