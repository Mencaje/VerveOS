.section .text

.global cpu_switch

.type cpu_switch, @function

/*
 * rdi: uint64_t *old_sp_save
 * rsi: uint64_t new_sp
 *
 * Matches System V AMD64 callee-saved set used in thread trampolines.
 */
cpu_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, (%rdi)

    mov %rsi, %rsp

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

.size cpu_switch, . - cpu_switch
