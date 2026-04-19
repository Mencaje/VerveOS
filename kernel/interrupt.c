#include <arch/i386/lapic.h>
#include <arch/i386/pic.h>
#include <verve/interrupt.h>
#include <verve/sched.h>
#include <verve/serial.h>

#include <stddef.h>
#include <stdint.h>

static int g_timer_lapic_eoi;

void irq_timer_set_lapic_eoi(int on)
{
    g_timer_lapic_eoi = on;
}

void verve_irq_dispatch(verve_exc_frame *f)
{
    uint32_t v = f->vec;

    if (v == 64u) {
        sched_timer_tick();
        lapic_eoi();
        return;
    }

    if (v >= 32u && v < 48u) {
        uint8_t irq = (uint8_t)(v - 32u);

        if (irq == 0)
            sched_timer_tick();

        if (g_timer_lapic_eoi)
            lapic_eoi();
        else
            pic_send_eoi(irq);

        return;
    }

    serial_puts("[VerveOS] irq: unexpected vector\r\n");

    for (;;)
        __asm__ volatile("hlt");
}

__attribute__((noreturn)) void verve_exc_dispatch(verve_exc_frame *f)
{
    serial_puts("\r\n[VerveOS] CPU exception vec=0x");
    serial_put_u64_hex((uint64_t)f->vec);
    serial_puts(" err=0x");
    serial_put_u64_hex((uint64_t)f->err);
    serial_puts("\r\n");

    serial_puts("[VerveOS] faulting EIP=0x");
    serial_put_u64_hex((uint64_t)f->eip);
    serial_puts(" CS=0x");
    serial_put_u64_hex((uint64_t)f->cs);
    serial_puts("\r\n");

    if (f->vec == 14) {
        uintptr_t cr2;

        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

        serial_puts("[VerveOS] #PF CR2=0x");
        serial_put_u64_hex((uint64_t)cr2);
        serial_puts("\r\n");
    }

    serial_puts("[VerveOS] halted after exception\r\n");

    for (;;)
        __asm__ volatile("hlt");
}
