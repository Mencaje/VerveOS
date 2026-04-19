#include <verve/acpi_smp.h>

#include <verve/acpi_hw.h>

uint32_t acpi_collect_cpu_apic_ids(const struct mb2_fixed *info,
    uint8_t *apic_ids_out, uint32_t max_ids)
{
    uint32_t n;
    uint32_t c;

    if (!apic_ids_out || max_ids == 0)
        return 0;

    if (!acpi_hw_init(info))
        return 0;

    n = acpi_hw_cpu_count();
    if (n == 0)
        return 0;

    c = n > max_ids ? max_ids : n;
    acpi_hw_copy_apic_ids(apic_ids_out, c);

    return c;
}
