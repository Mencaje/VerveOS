/*
 * CPU 异常 0–31 → 统一现场，由 verve_exc_dispatch 处理。
 * 与 i386/exc_asm.S 类似：带 / 不带 error code 两类入口。
 */
.code64
.section .text

.macro ISR_NOEC n
.global isr_\n\()
.align 16
isr_\n\():
    cli
    pushq $0
    pushq $\n
    jmp exc_common_push_regs
.endm

.macro ISR_EC n
.global isr_\n\()
.align 16
isr_\n\():
    cli
    pushq $\n
    jmp exc_common_push_regs
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

.global exc_common_push_regs
.align 16
exc_common_push_regs:
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
    call verve_exc_dispatch

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
