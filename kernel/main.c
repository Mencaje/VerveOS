#include <stdint.h>
#include <stddef.h>
#include <arch/i386/ioapic.h>
#include <arch/i386/lapic.h>
#include <arch/i386/pic.h>
#include <verve/acpi_hw.h>
#include <verve/interrupt.h>
#include <verve/mboot2.h>
#include <verve/heap.h>
#include <verve/paging.h>
#include <verve/pmm.h>
#include <verve/sched.h>
#include <verve/serial.h>
#include <verve/smp.h>
#include <verve/thread.h>

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_BUF  ((volatile uint16_t *)0xB8000)

static void vga_clear(uint8_t fg, uint8_t bg)
{
    uint16_t blank = (uint16_t)((bg << 12) | (fg << 8) | ' ');
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; ++i)
        VGA_BUF[i] = blank;
}

static void vga_puts(size_t row, size_t col, uint8_t fg, uint8_t bg, const char *s)
{
    uint16_t attr = (uint16_t)((bg << 12) | (fg << 8));
    size_t i = row * VGA_COLS + col;
    while (*s && i < VGA_COLS * VGA_ROWS)
        VGA_BUF[i++] = attr | (uint8_t)(*s++);
}

void kernel_main(uint32_t magic, uint32_t mb_info_phys)
{
    vga_clear(0x7, 0x0);
    vga_puts(0, 0, 0xA, 0x0, "VerveOS kernel");

    serial_puts("\r\n[VerveOS] kernel_main\r\n");

    if (magic != MB2_MAGIC) {
        serial_puts("[VerveOS] multiboot2: unexpected EAX magic\r\n");
        vga_puts(1, 0, 0xC, 0x0, "Multiboot2: unexpected magic");
        return;
    }

    serial_puts("[VerveOS] multiboot2: OK\r\n");

    const struct mb2_fixed *info =
        (const struct mb2_fixed *)(uintptr_t)mb_info_phys;

    uint64_t usable_mmap = 0;
    if (!mb2_sum_usable_ram(info, &usable_mmap)) {
        serial_puts("[VerveOS] mb2: memory map parse failed\r\n");
        vga_puts(1, 0, 0xF, 0x0, "Multiboot2: OK");
        vga_puts(2, 0, 0xC, 0x0, "mmap: parse error");
        return;
    }

    serial_puts("[VerveOS] mmap type=1 sum: ");
    serial_put_u64_hex(usable_mmap);
    serial_puts(" bytes\r\n");

    uint32_t lo_kb = 0;
    uint32_t hi_kb = 0;

    if (mb2_read_basic_meminfo(info, &lo_kb, &hi_kb)) {
        uint64_t approx = (uint64_t)lo_kb * 1024ULL
            + (uint64_t)hi_kb * 1024ULL;

        serial_puts("[VerveOS] basic meminfo: mem_lower_kb=0x");
        serial_put_u64_hex(lo_kb);
        serial_puts(" mem_upper_kb=0x");
        serial_put_u64_hex(hi_kb);
        serial_puts(" -> approx 0x");
        serial_put_u64_hex(approx);
        serial_puts(" bytes\r\n");

        serial_puts("[VerveOS] mmap sum is authoritative for frames; basic meminfo is legacy summary\r\n");
    } else {
        serial_puts("[VerveOS] basic meminfo: tag missing\r\n");
    }

    if (!pmm_init(info, mb_info_phys)) {
        serial_puts("[VerveOS] pmm_init failed\r\n");
        vga_puts(1, 0, 0xF, 0x0, "Multiboot2: OK");
        vga_puts(2, 0, 0xC, 0x0, "PMM: init failed");
        return;
    }

    serial_puts("[VerveOS] pmm: ready (bitmap PFN allocator)\r\n");

    heap_init();
    serial_puts("[VerveOS] heap: bump kmalloc arena (256 KiB BSS)\r\n");

    if (!paging_identity_init(pmm_max_pfn())) {
        serial_puts("[VerveOS] paging_identity_init failed\r\n");
        vga_puts(1, 0, 0xF, 0x0, "Multiboot2: OK");
        vga_puts(2, 0, 0xC, 0x0, "paging failed");
        return;
    }

    if (!acpi_hw_init(info))
        serial_puts("[VerveOS] acpi_hw: failed; using fallback single-CPU map\r\n");

    acpi_hotplug_stub_init();

    gdt_init();
    serial_puts("[VerveOS] gdt: flat ring0 segments loaded\r\n");

    idt_init();
    serial_puts("[VerveOS] idt: exceptions + IRQ + LAPIC timer vec 64\r\n");

    lapic_bsp_early_init();

    smp_bootstrap_apic_map();

    {
        uint32_t logical_cpus = acpi_hw_cpu_count();

        if (logical_cpus == 0u)
            logical_cpus = 1u;

        thread_subsystem_early_init(logical_cpus);
    }

    pic_init(0x20, 0x28);
    serial_puts("[VerveOS] pic: remapped master=0x20 slave=0x28\r\n");

    pic_irq_mask_all();
    serial_puts("[VerveOS] pic: all IRQ lines masked (timer via LAPIC)\r\n");

    if (ioapic_init_from_acpi()) {
        ioapic_route_irq0_timer(lapic_id());
        serial_puts("[VerveOS] ioapic: legacy PIT/IRQ0 routed (optional)\r\n");
    } else {
        serial_puts("[VerveOS] ioapic: not present or disabled in MADT\r\n");
    }

    sched_init();
    serial_puts("[VerveOS] sched: per-CPU ticks + LAPIC timer (vec 64)\r\n");

    smp_init(info);

    smp_bsp_timer_start();
    serial_puts("[VerveOS] lapic: BSP periodic timer armed\r\n");

    uintptr_t a = pmm_alloc_frame();

    serial_puts("[VerveOS] pmm_alloc_frame -> ");
    serial_put_u64_hex(a);
    serial_puts("\r\n");

    pmm_free_frame(a);

    uintptr_t b = pmm_alloc_frame();

    serial_puts("[VerveOS] pmm_alloc_frame (after free) -> ");
    serial_put_u64_hex(b);
    serial_puts("\r\n");

    vga_puts(1, 0, 0xF, 0x0, "Multiboot2: OK");
    vga_puts(2, 0, 0xE, 0x0, "sched + LAPIC");

    serial_puts("[VerveOS] enabling interrupts (sti), waiting for 5 BSP timer ticks...\r\n");

    __asm__ volatile("sti");

    while (sched_tick_count() < 5)
        __asm__ volatile("hlt");

    serial_puts("[VerveOS] sched: 5 BSP ticks seen; need_resched=");
    serial_put_u64_hex((uint64_t)(unsigned)sched_need_resched());
    serial_puts("\r\n");

    serial_puts("[VerveOS] clearing resched before thread demo\r\n");
    sched_clear_resched();

    serial_puts("[VerveOS] sti: LAPIC timer drives per-CPU need_resched\r\n");

    __asm__ volatile("sti");

    thread_demo_run();

    serial_puts("[VerveOS] cli after thread demo\r\n");

    __asm__ volatile("cli");

    serial_puts("[VerveOS] halted after thread demo\r\n");

    for (;;)
        __asm__ volatile("hlt");
}
