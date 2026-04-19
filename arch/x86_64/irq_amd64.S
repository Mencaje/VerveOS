/*
 * PIC 向量 32–47 与 LAPIC 定时器 64。现场布局与 exc 相同。
 */
.code64
.section .text

.macro IRQ n
.global irq_\n\()
.align 16
irq_\n\():
    cli
    pushq $0
    pushq $\n
    jmp irq_common_push_regs
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

.global irq_common_push_regs
.align 16
irq_common_push_regs:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    call verve_irq_dispatch

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp
    iretq
