#include <arch/x86_64/lapic.h>

#include <stdint.h>

#define IA32_APIC_BASE 0x1BU
#define APIC_BASE_EN   (1u << 11)

#define LAPIC_ID     0x020u
#define LAPIC_SVR    0x0F0u
#define LAPIC_EOI    0x0B0u
#define LAPIC_ICRLO  0x300u
#define LAPIC_ICRHI  0x310u

#define LAPIC_DIVCONF   0x3E0u
#define LAPIC_TIMER_ICR 0x380u
#define LAPIC_LVT_TIMER 0x320u

#define ICR_DELIVS  (1u << 12)

static uint8_t *g_lapic = (uint8_t *)(uintptr_t)LAPIC_DEFAULT_BASE;

static void mmio_w32(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(g_lapic + off) = v;
}

static uint32_t mmio_r32(uint32_t off)
{
    return *(volatile uint32_t *)(g_lapic + off);
}

void lapic_msr_enable(void)
{
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_APIC_BASE));
    lo |= APIC_BASE_EN;
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(IA32_APIC_BASE));
}

/*
 * LAPIC MMIO is covered by paging_amd64_identity_init span (must include
 * LAPIC_DEFAULT_BASE); no extra PTE walk here.
 */
void lapic_mmio_map(void) {}

void lapic_software_enable(void)
{
    uint32_t s = mmio_r32(LAPIC_SVR);

    s = (s & 0xFFFFFE00u) | 0x1FFu;
    s |= 0x100u;
    mmio_w32(LAPIC_SVR, s);
}

void lapic_eoi(void)
{
    mmio_w32(LAPIC_EOI, 0u);
}

uint8_t lapic_id(void)
{
    return (uint8_t)(mmio_r32(LAPIC_ID) >> 24);
}

void lapic_icr_wait(void)
{
    while ((mmio_r32(LAPIC_ICRLO) & ICR_DELIVS) != 0u)
        __asm__ volatile("pause" ::: "memory");
}

void lapic_send_init(uint8_t apic_id)
{
    mmio_w32(LAPIC_ICRHI, (uint32_t)apic_id << 24);
    mmio_w32(LAPIC_ICRLO, 0x4500u);
    lapic_icr_wait();
}

void lapic_send_sipi(uint8_t apic_id, uint8_t vector)
{
    mmio_w32(LAPIC_ICRHI, (uint32_t)apic_id << 24);
    mmio_w32(LAPIC_ICRLO, 0x600u | (uint32_t)vector);
    lapic_icr_wait();
}

void lapic_bsp_early_init(void)
{
    lapic_mmio_map();
    lapic_msr_enable();
    lapic_software_enable();
}

void lapic_timer_periodic(uint8_t vector, uint32_t initial_count, uint8_t divide_reg)
{
    mmio_w32(LAPIC_LVT_TIMER, 0x10000u | (uint32_t)vector);
    mmio_w32(LAPIC_DIVCONF, (uint32_t)divide_reg);
    mmio_w32(LAPIC_TIMER_ICR, initial_count);
    mmio_w32(LAPIC_LVT_TIMER, 0x20000u | (uint32_t)vector);
}
