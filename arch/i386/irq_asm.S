.section .text

.macro IRQ vec
.global irq_\vec
.align 16
irq_\vec:
    cli
    push $0
    push $\vec
    jmp irq_common
.endm

IRQ 32
IRQ 33
IRQ 34
IRQ 35
IRQ 36
IRQ 37
IRQ 38
IRQ 39
IRQ 40
IRQ 41
IRQ 42
IRQ 43
IRQ 44
IRQ 45
IRQ 46
IRQ 47

IRQ 64

.global irq_common
.align 16
irq_common:
    pusha
    push %esp
    call verve_irq_dispatch
    add $4, %esp
    popa
    add $8, %esp
    iret
