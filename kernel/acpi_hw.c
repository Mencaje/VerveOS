#include <verve/acpi_hw.h>

#include <verve/mboot2.h>
#include <verve/serial.h>

#include <stddef.h>
#include <stdint.h>

#define MAX_CPUS_HW 32u

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oem_id[6];
    uint8_t oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_hdr_t;

#define RSDP_SIG "RSD PTR "

static uint8_t g_cpu_apic_ids[MAX_CPUS_HW];
static uint32_t g_cpu_count;

static uint32_t g_ioapic_phys;
static uint32_t g_ioapic_gsi_base;
static uint32_t g_irq0_gsi;
static uint8_t g_ioapic_id;
static bool g_has_ioapic;
static bool g_hw_inited;

static uint8_t acpi_sum(const uint8_t *p, uint32_t len)
{
    uint32_t s = 0;
    uint32_t i;

    for (i = 0; i < len; ++i)
        s += p[i];

    return (uint8_t)(s & 0xFFu);
}

static bool acpi_ok(const uint8_t *p, uint32_t len)
{
    return acpi_sum(p, len) == 0;
}

static bool rsdp_parse(const uint8_t *rsdp, size_t rsdp_len,
    uint32_t *rsdt_out, uint64_t *xsdt_out, int *uses_xsdt)
{
    uint32_t rl;
    uint64_t xl;

    if (rsdp_len < 20u)
        return false;

    {
        size_t i;

        for (i = 0; i < 8u; ++i) {
            if (rsdp[i] != (uint8_t)RSDP_SIG[i])
                return false;
        }
    }

    if (!acpi_ok(rsdp, 20u))
        return false;

    *rsdt_out = *(const uint32_t *)(rsdp + 16u);
    *xsdt_out = 0u;
    *uses_xsdt = 0;

    if (rsdp_len >= 36u && rsdp[15u] >= 2u) {
        rl = *(const uint32_t *)(rsdp + 20u);
        (void)rl;

        if (rsdp_len >= 36u) {
            xl = *(const uint64_t *)(rsdp + 24u);
            if (xl != 0u) {
                *xsdt_out = xl;
                *uses_xsdt = 1;
            }
        }
    }

    return true;
}

static bool tbl_ok(const uint8_t *t, uint32_t scan_len)
{
    const acpi_hdr_t *h = (const acpi_hdr_t *)t;

    if (scan_len < sizeof(acpi_hdr_t))
        return false;

    if (h->length < sizeof(acpi_hdr_t) || h->length > scan_len)
        return false;

    return acpi_ok(t, h->length);
}

static bool madt_collect(const uint8_t *m, uint32_t mlen)
{
    const uint32_t madt_head = 44u;
    uint32_t off;
    uint32_t lapic_mmio_def = 0;
    uint32_t ncpu = 0;

    if (mlen < madt_head)
        return false;

    lapic_mmio_def = *(const uint32_t *)(m + 36u);
    (void)lapic_mmio_def;

    g_irq0_gsi = 0u;
    g_has_ioapic = false;

    off = madt_head;

    while (off + 2u <= mlen) {
        uint8_t typ = m[off];
        uint8_t elen = m[off + 1u];

        if (elen < 2u || off + elen > mlen)
            return false;

        if (typ == 0u && elen >= 8u) {
            uint32_t fl = *(const uint32_t *)(m + off + 4u);

            if ((fl & 1u) != 0u && ncpu < MAX_CPUS_HW)
                g_cpu_apic_ids[ncpu++] = m[off + 3u];
        } else if (typ == 1u && elen >= 12u && !g_has_ioapic) {
            g_ioapic_id = m[off + 2u];
            g_ioapic_phys = *(const uint32_t *)(m + off + 4u);
            g_ioapic_gsi_base = *(const uint32_t *)(m + off + 8u);
            g_has_ioapic = true;
            (void)g_ioapic_id;
        } else if (typ == 2u && elen >= 16u) {
            uint8_t src_irq = m[off + 8u];

            if (src_irq == 0u)
                g_irq0_gsi = *(const uint32_t *)(m + off + 12u);
        }

        off += elen;
    }

    g_cpu_count = ncpu;
    return true;
}

static bool scan_root(bool xsdt, uint64_t root_phys)
{
    const uint8_t *tbl = (const uint8_t *)(uintptr_t)root_phys;
    const acpi_hdr_t *h = (const acpi_hdr_t *)tbl;
    uint32_t body;
    uint32_t i;

    if (!tbl_ok(tbl, h->length))
        return false;

    body = h->length - (uint32_t)sizeof(acpi_hdr_t);

    if (xsdt) {
        uint32_t n = body / 8u;

        for (i = 0; i < n; ++i) {
            uint64_t tp = *(const uint64_t *)(tbl + sizeof(acpi_hdr_t) + i * 8u);
            const uint8_t *st;
            const acpi_hdr_t *sh;

            if (tp == 0u)
                continue;

            if (tp > 0xFFFFFFFFull)
                continue;

            st = (const uint8_t *)(uintptr_t)tp;
            sh = (const acpi_hdr_t *)st;

            if (!tbl_ok(st, sh->length))
                continue;

            if (sh->signature[0] == 'A' && sh->signature[1] == 'P'
                && sh->signature[2] == 'I' && sh->signature[3] == 'C')
                return madt_collect(st, sh->length);
        }
    } else {
        uint32_t n = body / 4u;

        for (i = 0; i < n; ++i) {
            uint32_t tp = *(const uint32_t *)(tbl + sizeof(acpi_hdr_t)
                + i * 4u);
            const uint8_t *st;
            const acpi_hdr_t *sh;

            if (tp == 0u)
                continue;

            st = (const uint8_t *)(uintptr_t)tp;
            sh = (const acpi_hdr_t *)st;

            if (!tbl_ok(st, sh->length))
                continue;

            if (sh->signature[0] == 'A' && sh->signature[1] == 'P'
                && sh->signature[2] == 'I' && sh->signature[3] == 'C')
                return madt_collect(st, sh->length);
        }
    }

    return false;
}

bool acpi_hw_init(const struct mb2_fixed *info)
{
    uint8_t rsdp_buf[128];
    size_t rsdp_len = 0;
    uint32_t rsdt_phys = 0;
    uint64_t xsdt_phys = 0;
    int uses_xsdt = 0;

    if (g_hw_inited)
        return true;

    if (!mb2_find_rsdp_payload(info, rsdp_buf, sizeof(rsdp_buf), &rsdp_len))
        return false;

    if (!rsdp_parse(rsdp_buf, rsdp_len, &rsdt_phys, &xsdt_phys, &uses_xsdt))
        return false;

    if (uses_xsdt != 0 && xsdt_phys != 0u && xsdt_phys <= 0xFFFFFFFFull) {
        if (scan_root(true, xsdt_phys))
            goto ok;
    }

    if (rsdt_phys != 0u && scan_root(false, (uint64_t)rsdt_phys))
        goto ok;

    serial_puts("[VerveOS] acpi_hw: no valid MADT found\r\n");
    return false;

ok:
    g_hw_inited = true;
    serial_puts("[VerveOS] acpi_hw: MADT cpus=");
    serial_put_u64_hex((uint64_t)g_cpu_count);
    serial_puts(" ioapic=");
    serial_put_u64_hex((uint64_t)(unsigned)g_has_ioapic);
    serial_puts(" irq0_gsi=");
    serial_put_u64_hex((uint64_t)g_irq0_gsi);
    serial_puts("\r\n");

    return true;
}

uint32_t acpi_hw_cpu_count(void)
{
    return g_cpu_count;
}

void acpi_hw_copy_apic_ids(uint8_t *out, uint32_t max)
{
    uint32_t i;
    uint32_t n = g_cpu_count;

    if (!out || max == 0)
        return;

    if (n > max)
        n = max;

    for (i = 0; i < n; ++i)
        out[i] = g_cpu_apic_ids[i];
}

bool acpi_hw_has_ioapic(void)
{
    return g_has_ioapic;
}

uint32_t acpi_hw_ioapic_phys(void)
{
    return g_ioapic_phys;
}

uint32_t acpi_hw_ioapic_gsi_base(void)
{
    return g_ioapic_gsi_base;
}

uint32_t acpi_hw_irq0_gsi(void)
{
    return g_irq0_gsi;
}

void acpi_hotplug_stub_init(void)
{
}
