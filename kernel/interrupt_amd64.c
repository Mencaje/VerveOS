#include <arch/x86_64/lapic.h>
#include <arch/x86_64/pic.h>
#include <verve/amd64.h>
#include <verve/interrupt.h>
#include <verve/sched.h>
#include <verve/serial.h>

#include <stdint.h>

static int g_timer_lapic_eoi;

void irq_timer_set_lapic_eoi(int on)
{
    g_timer_lapic_eoi = on ? 1 : 0;
}

static volatile uint64_t g_pic_ticks;

uint64_t amd64_pic_timer_ticks(void)
{
    return g_pic_ticks;
}

void verve_irq_dispatch(verve_exc_frame *f)
{
    uint64_t v = f->vec;

    if (v == 64u) {
        sched_timer_tick();
        lapic_eoi();
        return;
    }

    if (v >= 32u && v < 48u) {
        uint8_t irq = (uint8_t)(v - 32u);

        if (irq == 0u && !g_timer_lapic_eoi) {
            ++g_pic_ticks;
            sched_timer_tick();
        }

        if (g_timer_lapic_eoi)
            lapic_eoi();
        else
            pic_send_eoi(irq);

        return;
    }

    serial_puts("[VerveOS] amd64 irq: unexpected vec=0x");
    serial_put_u64_hex(v);
    serial_puts("\r\n");

    for (;;)
        __asm__ volatile("hlt");
}

__attribute__((noreturn)) void verve_exc_dispatch(verve_exc_frame *f)
{
    serial_puts("\r\n[VerveOS] amd64 exception vec=0x");
    serial_put_u64_hex(f->vec);
    serial_puts(" err=0x");
    serial_put_u64_hex(f->err);
    serial_puts("\r\n");

    serial_puts("[VerveOS] RIP=0x");
    serial_put_u64_hex(f->rip);
    serial_puts(" CS=0x");
    serial_put_u64_hex(f->cs);
    serial_puts(" RFLAGS=0x");
    serial_put_u64_hex(f->rflags);
    serial_puts("\r\n");

    if (f->vec == 14u) {
        uintptr_t cr2;

        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

        serial_puts("[VerveOS] #PF CR2=0x");
        serial_put_u64_hex((uint64_t)cr2);
        serial_puts("\r\n");
    }

    serial_puts("[VerveOS] halted after exception (amd64)\r\n");

    for (;;)
        __asm__ volatile("hlt");
}
