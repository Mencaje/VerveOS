#pragma once

#include <stdint.h>

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

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void verve_exc_dispatch(verve_exc_frame *f);

void verve_irq_dispatch(verve_exc_frame *f);

void gdt_init(void);

void idt_init(void);

void irq_timer_set_lapic_eoi(int on);
