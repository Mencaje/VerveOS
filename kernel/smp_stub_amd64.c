#include <verve/mboot2.h>
#include <verve/smp.h>

#include <stdint.h>

/*
 * BSP-only bring-up for amd64 (no LAPIC/AP yet). Same API as kernel/smp.c.
 */
void smp_bootstrap_apic_map(void) {}

uint32_t smp_cpu_index(void)
{
    return 0u;
}

void smp_bsp_timer_start(void) {}

void smp_init(const struct mb2_fixed *info)
{
    (void)info;
}

uint32_t smp_cpu_count(void)
{
    return 1u;
}
