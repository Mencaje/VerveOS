#include <verve/smp.h>

#include <arch/i386/lapic.h>
#include <verve/acpi_hw.h>
#include <verve/interrupt.h>
#include <verve/paging.h>
#include <verve/pmm.h>
#include <verve/serial.h>
#include <verve/smp_boot.h>
#include <verve/thread.h>

#include <stddef.h>
#include <stdint.h>

#define MAX_APIC 32u

#define LAPIC_TIMER_VEC   64u
#define LAPIC_TIMER_INIT  125000u
#define LAPIC_TIMER_DIVCFG 3u

extern char _binary_build_smp_tramp_bin_start[];
extern char _binary_build_smp_tramp_bin_end[];

static uint32_t g_cpus_online = 1u;

static uint8_t g_apic_to_logical[256];

static uint32_t read_cr3(void)
{
    uint32_t v;

    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void smp_udelay_ms(unsigned ms)
{
    volatile unsigned i;
    volatile unsigned j;

    for (i = 0; i < ms * 4000u; ++i) {
        for (j = 0; j < 1000u; ++j)
            __asm__ volatile("" ::: "memory");
    }
}

static void smp_udelay_short(void)
{
    volatile unsigned i;

    for (i = 0; i < 80000u; ++i)
        __asm__ volatile("" ::: "memory");
}

static void smp_install_trampoline(void)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)VERVE_SMP_TRAMP_PHYS;
    uint8_t *s = (uint8_t *)_binary_build_smp_tramp_bin_start;
    uint8_t *e = (uint8_t *)_binary_build_smp_tramp_bin_end;

    while (s < e)
        *dst++ = *s++;
}

static void verve_ap_entry(void);

void smp_bootstrap_apic_map(void)
{
    uint8_t ids[MAX_APIC];
    uint32_t n;
    uint32_t i;

    for (i = 0; i < 256u; ++i)
        g_apic_to_logical[i] = 0xFFu;

    n = acpi_hw_cpu_count();

    if (n == 0u) {
        ids[0] = lapic_id();
        n = 1u;
    } else {
        if (n > MAX_APIC)
            n = MAX_APIC;

        acpi_hw_copy_apic_ids(ids, n);
    }

    for (i = 0; i < n; ++i)
        g_apic_to_logical[ids[i]] = (uint8_t)i;
}

uint32_t smp_cpu_index(void)
{
    uint8_t id = lapic_id();
    uint8_t ix = g_apic_to_logical[id];

    if (ix == 0xFFu)
        return 0u;

    return (uint32_t)ix;
}

static void smp_start_lapic_timer(void)
{
    irq_timer_set_lapic_eoi(1);
    lapic_timer_periodic((uint8_t)LAPIC_TIMER_VEC, LAPIC_TIMER_INIT,
        (uint8_t)LAPIC_TIMER_DIVCFG);
}

void smp_init(const struct mb2_fixed *info)
{
    uint8_t cpu_ids[MAX_APIC];
    uint32_t ncpu;
    uint32_t i;
    uint8_t bsp_id;
    struct verve_smp_mailbox *mail =
        (struct verve_smp_mailbox *)(uintptr_t)VERVE_SMP_MAIL_PHYS;

    (void)info;

    g_cpus_online = 1u;

    serial_puts("[VerveOS] SMP: using ACPI hw CPU list\r\n");

    ncpu = acpi_hw_cpu_count();

    if (ncpu == 0) {
        serial_puts("[VerveOS] SMP: acpi reports 0 cpus; BSP-only\r\n");
        return;
    }

    acpi_hw_copy_apic_ids(cpu_ids, ncpu > MAX_APIC ? MAX_APIC : ncpu);
    if (ncpu > MAX_APIC)
        ncpu = MAX_APIC;

    paging_map_identity_page((uintptr_t)VERVE_SMP_TRAMP_PHYS);
    paging_map_identity_page((uintptr_t)VERVE_SMP_MAIL_PHYS);

    smp_install_trampoline();

    bsp_id = lapic_id();

    serial_puts("[VerveOS] SMP: BSP Local APIC id=");
    serial_put_u64_hex((uint64_t)bsp_id);
    serial_puts("\r\n");

    for (i = 0; i < ncpu; ++i) {
        uintptr_t stk_phys;
        uint32_t stk_top;

        if (cpu_ids[i] == bsp_id)
            continue;

        stk_phys = pmm_alloc_frame();
        if (stk_phys == 0) {
            serial_puts("[VerveOS] SMP: pmm_alloc_frame(AP stack) failed\r\n");
            continue;
        }

        stk_top = (uint32_t)(stk_phys + 4096u);

        mail->cr3_phys = read_cr3();
        mail->esp = stk_top;
        mail->entry = (uint32_t)(uintptr_t)verve_ap_entry;

        __asm__ volatile("" ::: "memory");

        lapic_send_init(cpu_ids[i]);
        smp_udelay_ms(12u);

        lapic_send_sipi(cpu_ids[i], (uint8_t)(VERVE_SMP_TRAMP_PHYS >> 12));
        smp_udelay_short();

        lapic_send_sipi(cpu_ids[i], (uint8_t)(VERVE_SMP_TRAMP_PHYS >> 12));
        smp_udelay_ms(2u);

        g_cpus_online++;
        serial_puts("[VerveOS] SMP: SIPI sent to APIC id=");
        serial_put_u64_hex((uint64_t)cpu_ids[i]);
        serial_puts("\r\n");
    }

    serial_puts("[VerveOS] SMP: bringing APs online done (cpus_online=");
    serial_put_u64_hex((uint64_t)g_cpus_online);
    serial_puts(")\r\n");
}

uint32_t smp_cpu_count(void)
{
    return g_cpus_online;
}

static void verve_ap_entry(void)
{
    lapic_mmio_map();
    lapic_msr_enable();
    lapic_software_enable();

    serial_puts("[VerveOS] SMP: AP entered, lapic id=");
    serial_put_u64_hex((uint64_t)lapic_id());
    serial_puts("\r\n");

    smp_start_lapic_timer();

    thread_ap_online();
}

void smp_bsp_timer_start(void)
{
    smp_start_lapic_timer();
}
