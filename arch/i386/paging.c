#include <verve/paging.h>

#include <stddef.h>
#include <stdint.h>

#include <verve/serial.h>

#define PTE_PRESENT (1u << 0)
#define PTE_RW      (1u << 1)

#define PT_ENTRIES 1024u

#define MAX_ID_PT 128
#define MMIO_PT_SLOTS 16

static uint32_t boot_pd[1024] __attribute__((aligned(4096)));
static uint32_t boot_pt[MAX_ID_PT][1024] __attribute__((aligned(4096)));

typedef struct {
    uint32_t used;
    uint32_t pd_idx;
    uint32_t tbl[1024];
} mmio_pt_slot_t;

static mmio_pt_slot_t g_mmio_pts[MMIO_PT_SLOTS];

static uint32_t *mmio_pt_for_pd(uint32_t pd_idx)
{
    size_t i;

    if (boot_pd[pd_idx] != 0u)
        return (uint32_t *)(uintptr_t)((uintptr_t)boot_pd[pd_idx]
            & 0xFFFFF000u);

    for (i = 0; i < MMIO_PT_SLOTS; ++i) {
        if (g_mmio_pts[i].used != 0u && g_mmio_pts[i].pd_idx == pd_idx)
            return g_mmio_pts[i].tbl;
    }

    for (i = 0; i < MMIO_PT_SLOTS; ++i) {
        size_t k;

        if (g_mmio_pts[i].used != 0u)
            continue;

        for (k = 0; k < 1024u; ++k)
            g_mmio_pts[i].tbl[k] = 0u;

        g_mmio_pts[i].pd_idx = pd_idx;
        g_mmio_pts[i].used = 1u;

        boot_pd[pd_idx] =
            (((uint32_t)(uintptr_t)g_mmio_pts[i].tbl) & 0xFFFFF000u)
            | PTE_PRESENT | PTE_RW;

        return g_mmio_pts[i].tbl;
    }

    return NULL;
}

static void cr3_load(uintptr_t pd_phys)
{
    uint32_t p = (uint32_t)pd_phys;

    __asm__ volatile("mov %0, %%cr3" : : "r"(p) : "memory");
}

static void paging_enable_if_needed(void)
{
    uint32_t cr0;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    if ((cr0 & 0x80000000u) != 0)
        return;

    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

bool paging_identity_init(uint32_t max_pfn)
{
    uint32_t pfns = max_pfn + 1u;
    uint32_t n_pt = (pfns + PT_ENTRIES - 1u) / PT_ENTRIES;

    if (n_pt > MAX_ID_PT) {
        serial_puts("[VerveOS] paging: RAM span exceeds static identity tables; "
                    "raise MAX_ID_PT in arch/i386/paging.c\r\n");
        return false;
    }

    for (size_t i = 0; i < 1024u; ++i)
        boot_pd[i] = 0;

    for (uint32_t ti = 0; ti < n_pt; ++ti) {
        uintptr_t pt_phys = (uintptr_t)&boot_pt[ti][0];

        boot_pd[ti] = (uint32_t)(pt_phys | PTE_PRESENT | PTE_RW);

        for (uint32_t pi = 0; pi < PT_ENTRIES; ++pi) {
            uint32_t pfn = ti * PT_ENTRIES + pi;

            if (pfn >= pfns)
                boot_pt[ti][pi] = 0;
            else {
                uintptr_t addr = ((uintptr_t)pfn) << 12;

                boot_pt[ti][pi] =
                    ((uint32_t)addr & 0xFFFFF000u) | PTE_PRESENT | PTE_RW;
            }
        }
    }

    cr3_load((uintptr_t)&boot_pd[0]);
    paging_enable_if_needed();

    serial_puts("[VerveOS] paging: identity map CR3=0x");
    serial_put_u64_hex((uintptr_t)&boot_pd[0]);
    serial_puts(" PTEs=");
    serial_put_u64_hex((uint64_t)pfns);
    serial_puts("\r\n");

    return true;
}

bool paging_map_identity_page(uintptr_t phys)
{
    uint32_t pd_idx = (uint32_t)((uintptr_t)phys >> 22);
    uint32_t pt_idx = ((uint32_t)((uintptr_t)phys >> 12)) & 0x3FFu;
    uint32_t pte = ((uint32_t)(uintptr_t)phys & 0xFFFFF000u) | PTE_PRESENT | PTE_RW;
    uint32_t *pt;

    pt = mmio_pt_for_pd(pd_idx);
    if (!pt)
        return false;

    pt[pt_idx] = pte;

    __asm__ volatile("invlpg (%0)" : : "r"(phys) : "memory");

    return true;
}
