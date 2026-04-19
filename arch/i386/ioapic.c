#include <arch/i386/ioapic.h>

#include <verve/acpi_hw.h>
#include <verve/paging.h>
#include <verve/serial.h>

#include <stdint.h>

#define IOREGSEL 0x00u
#define IOREGWIN 0x10u

static uint32_t g_ioapic_base;

static void ioapic_mmio_write(uint32_t base, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)(base + IOREGSEL) = reg;
    *(volatile uint32_t *)(uintptr_t)(base + IOREGWIN) = val;
}

static uint32_t ioapic_mmio_read(uint32_t base, uint32_t reg)
{
    *(volatile uint32_t *)(uintptr_t)(base + IOREGSEL) = reg;

    return *(volatile uint32_t *)(uintptr_t)(base + IOREGWIN);
}

static void ioapic_set_rte(uint32_t base, uint32_t gsi_ix,
    uint32_t low, uint32_t high)
{
    uint32_t ix = 0x10u + gsi_ix * 2u;

    ioapic_mmio_write(base, ix + 1u, high);
    ioapic_mmio_write(base, ix, low);

    (void)ioapic_mmio_read(base, ix);
}

bool ioapic_init_from_acpi(void)
{
    uint32_t phys;

    if (!acpi_hw_has_ioapic())
        return false;

    phys = acpi_hw_ioapic_phys();
    if (phys == 0u)
        return false;

    paging_map_identity_page((uintptr_t)(phys & 0xFFFFF000u));

    g_ioapic_base = phys;

    serial_puts("[VerveOS] ioapic: MMIO @phys 0x");
    serial_put_u64_hex((uint64_t)phys);
    serial_puts("\r\n");

    return true;
}

void ioapic_route_irq0_timer(uint8_t bsp_apic_id)
{
    uint32_t gsi = acpi_hw_irq0_gsi();
    uint32_t gsi_base = acpi_hw_ioapic_gsi_base();
    uint32_t idx;
    uint32_t low;
    uint32_t high;

    if (g_ioapic_base == 0u)
        return;

    if (gsi < gsi_base)
        gsi = gsi_base;

    idx = gsi - gsi_base;

    low = 32u;
    low |= (0u << 8);
    low |= (0u << 11);
    low |= (0u << 13);
    low |= (0u << 15);

    high = (uint32_t)bsp_apic_id << 24;

    ioapic_set_rte(g_ioapic_base, idx, low, high);

    serial_puts("[VerveOS] ioapic: IRQ0/timer -> vec32 gsi_idx=");
    serial_put_u64_hex((uint64_t)idx);
    serial_puts(" BSP_apic=");
    serial_put_u64_hex((uint64_t)bsp_apic_id);
    serial_puts("\r\n");
}
