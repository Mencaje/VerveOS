.section .text

.global cpu_switch

.type cpu_switch, @function

cpu_switch:
    mov 4(%esp), %eax
    mov 8(%esp), %edx

    push %ebp
    push %ebx
    push %esi
    push %edi

    mov %esp, (%eax)

    mov %edx, %esp

    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

.size cpu_switch, . - cpu_switch
