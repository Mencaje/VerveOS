#include <arch/i386/pic.h>
#include <verve/interrupt.h>
#include <verve/sched.h>
#include <verve/serial.h>

#include <stddef.h>
#include <stdint.h>

__attribute__((cdecl)) void verve_irq_dispatch(verve_exc_frame *f)
{
    uint32_t v = f->vec;

    if (v >= 32u && v < 48u) {
        uint8_t irq = (uint8_t)(v - 32u);

        if (irq == 0)
            sched_timer_tick();

        pic_send_eoi(irq);
        return;
    }

    serial_puts("[VerveOS] irq: unexpected vector\r\n");

    for (;;)
        __asm__ volatile("hlt");
}

__attribute__((cdecl, noreturn)) void verve_exc_dispatch(verve_exc_frame *f)
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
