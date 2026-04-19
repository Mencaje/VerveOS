#include <arch/x86_64/lapic.h>
#include <arch/x86_64/pic.h>
#include <verve/heap.h>
#include <verve/interrupt.h>
#include <verve/mboot2.h>
#include <verve/paging.h>
#include <verve/pmm.h>
#include <verve/sched.h>
#include <verve/serial.h>
#include <verve/thread.h>

#include <stddef.h>
#include <stdint.h>

#define VGA_BUF ((volatile uint16_t *)0xB8000u)

struct max_phys_ctx {
    uint64_t max_end;
};

static void max_phys_cb(uint64_t base, uint64_t len, void *ctx)
{
    struct max_phys_ctx *m = ctx;
    uint64_t end = base + len;

    if (end > m->max_end)
        m->max_end = end;
}

void kernel_main_amd64(uint64_t magic, uint64_t mb_info_phys)
{
    VGA_BUF[0] = (uint16_t)((0x2u << 12) | (0xAu << 8) | '6');
    VGA_BUF[1] = (uint16_t)((0x2u << 12) | (0xAu << 8) | '4');

    serial_puts("\r\n[VerveOS] kernel_main_amd64 (long mode)\r\n");

    if (magic != MB2_MAGIC) {
        serial_puts("[VerveOS] multiboot2: unexpected RAX magic\r\n");
        return;
    }

    serial_puts("[VerveOS] multiboot2: OK (x86_64)\r\n");

    const struct mb2_fixed *info =
        (const struct mb2_fixed *)(uintptr_t)mb_info_phys;

    {
        uint64_t usable_mmap = 0;

        if (!mb2_sum_usable_ram(info, &usable_mmap))
            serial_puts("[VerveOS] mmap sum: parse failed\r\n");
        else {
            serial_puts("[VerveOS] mmap type=1 sum: ");
            serial_put_u64_hex(usable_mmap);
            serial_puts(" bytes\r\n");
        }
    }

    /* Upper bound of physical RAM from mmap for identity map + PMM */
    struct max_phys_ctx mph = {0};
    if (!mb2_for_each_available(info, max_phys_cb, &mph) || mph.max_end == 0) {
        serial_puts("[VerveOS] mmap: cannot determine max physical address\r\n");
        return;
    }

    {
        /* xAPIC MMIO is not in type-1 RAM; ensure identity map covers it. */
        uint64_t map_end = mph.max_end;
        const uint64_t lapic_end = (uint64_t)LAPIC_DEFAULT_BASE + 0x1000ull;

        if (map_end < lapic_end)
            map_end = lapic_end;

        if (!paging_amd64_identity_init(map_end)) {
            serial_puts("[VerveOS] paging_amd64_identity_init failed (span too "
                        "large for static PT pool?)\r\n");
            return;
        }
    }

    serial_puts("[VerveOS] amd64 paging: CR3 reload (2 MiB identity)\r\n");

    if (!pmm_init(info, (uint32_t)mb_info_phys)) {
        serial_puts("[VerveOS] pmm_init failed\r\n");
        return;
    }

    serial_puts("[VerveOS] pmm: ready; frames=");
    serial_put_u64_hex(pmm_frame_count());
    serial_puts(" max_pfn=");
    serial_put_u64_hex(pmm_max_pfn());
    serial_puts("\r\n");

    heap_init();
    serial_puts("[VerveOS] heap: bump kmalloc arena (256 KiB BSS)\r\n");

    idt_init();
    serial_puts("[VerveOS] amd64 IDT: loaded (exceptions 0–31, IRQ stubs 32–47, vec 64)\r\n");

    lapic_bsp_early_init();
    serial_puts("[VerveOS] amd64 LAPIC: BSP MMIO + SW enable\r\n");

    sched_init();
    serial_puts("[VerveOS] sched: per-CPU ticks (LAPIC periodic → vec 64)\r\n");

    thread_system_init();

    /*
     * Remap 8259 then mask all lines (same as i386 LAPIC-only timer path).
     */
    pic_init(0x20u, 0x28u);
    pic_irq_mask_all();
    serial_puts("[VerveOS] amd64 PIC: remapped + all IRQ masked\r\n");

    irq_timer_set_lapic_eoi(1);
    lapic_timer_periodic((uint8_t)LAPIC_TIMER_VEC, LAPIC_TIMER_INIT,
        (uint8_t)LAPIC_TIMER_DIVCFG);
    serial_puts("[VerveOS] amd64 LAPIC timer: periodic vec 64 armed\r\n");

    serial_puts("[VerveOS] enabling interrupts (sti); wait 5 BSP timer ticks...\r\n");

    __asm__ volatile("sti");

    while (sched_tick_count() < 5)
        __asm__ volatile("hlt");

    serial_puts("[VerveOS] sched: 5 ticks seen; need_resched=");
    serial_put_u64_hex((uint64_t)(unsigned)sched_need_resched());
    serial_puts("\r\n");

    serial_puts("[VerveOS] clearing resched before thread demo\r\n");
    sched_clear_resched();

    serial_puts("[VerveOS] sti: LAPIC drives per-CPU need_resched\r\n");

    __asm__ volatile("sti");

    thread_demo_run();

    serial_puts("[VerveOS] cli after thread demo\r\n");

    __asm__ volatile("cli");

    serial_puts("[VerveOS] halted after thread demo (amd64)\r\n");

    for (;;)
        __asm__ volatile("hlt");
}
