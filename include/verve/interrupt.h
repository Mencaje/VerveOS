#pragma once

#include <stdint.h>

#if defined(__x86_64__)

/* x86_64 ISR 现场：与 arch/x86_64/exc_amd64.S / irq_amd64.S 布局一致（r15→…→rax→vec→err→iret 帧） */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vec;
    uint64_t err;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} verve_exc_frame;

#else

typedef struct {
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp_old;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t vec;
    uint32_t err;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} verve_exc_frame;

#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void verve_exc_dispatch(verve_exc_frame *f);

void verve_irq_dispatch(verve_exc_frame *f);

void gdt_init(void);

void idt_init(void);

void irq_timer_set_lapic_eoi(int on);
