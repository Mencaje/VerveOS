.section .text

.global verve_enter_user

/*
 * void verve_enter_user(uint32_t user_eip, uint32_t user_esp);
 * CPL 0 -> iret -> Ring 3 at user CS/SS.
 */
verve_enter_user:
    mov 4(%esp), %eax
    mov 8(%esp), %edx

    pushl $0x23
    pushl %edx
    pushl $0x202
    pushl $0x1B
    pushl %eax
    iret
