.section .text

.macro ISR_NOEC n
.global isr_\n
.align 16
isr_\n:
    cli
    push $0
    push $\n
    jmp isr_common
.endm

.macro ISR_EC n
.global isr_\n
.align 16
isr_\n:
    cli
    push $\n
    jmp isr_common
.endm

ISR_NOEC 0
ISR_NOEC 1
ISR_NOEC 2
ISR_NOEC 3
ISR_NOEC 4
ISR_NOEC 5
ISR_NOEC 6
ISR_NOEC 7
ISR_EC   8
ISR_NOEC 9
ISR_EC   10
ISR_EC   11
ISR_EC   12
ISR_EC   13
ISR_EC   14
ISR_NOEC 15
ISR_NOEC 16
ISR_EC   17
ISR_NOEC 18
ISR_NOEC 19
ISR_NOEC 20
ISR_NOEC 21
ISR_NOEC 22
ISR_NOEC 23
ISR_NOEC 24
ISR_NOEC 25
ISR_NOEC 26
ISR_NOEC 27
ISR_NOEC 28
ISR_NOEC 29
ISR_NOEC 30
ISR_NOEC 31

.global isr_common
.align 16
isr_common:
    pusha
    push %esp
    call verve_exc_dispatch
    add $4, %esp
    popa
    add $8, %esp
    iret
