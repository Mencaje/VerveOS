#include <verve/paging.h>

#include <stddef.h>
#include <stdint.h>

#include <verve/pmm.h>
#include <verve/serial.h>

#define PTE_PRESENT (1u << 0)
#define PTE_RW      (1u << 1)
#define PTE_US      (1u << 2)

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

uintptr_t paging_get_cr3(void)
{
    uintptr_t cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    return cr3 & 0xFFFFF000u;
}

void paging_switch_cr3(uintptr_t pd_phys)
{
    cr3_load(pd_phys & 0xFFFFF000u);
}

static bool pte_fetch_pd(uintptr_t pd_phys, uintptr_t virt, uint32_t *pte_out)
{
    uint32_t *pd = (uint32_t *)(uintptr_t)pd_phys;
    uint32_t pd_idx = (uint32_t)((uintptr_t)virt >> 22);
    uint32_t pt_idx = ((uint32_t)((uintptr_t)virt >> 12)) & 0x3FFu;
    uint32_t pd_e = pd[pd_idx];
    uint32_t *pt;

    if ((pd_e & PTE_PRESENT) == 0)
        return false;

    pt = (uint32_t *)(uintptr_t)(pd_e & 0xFFFFF000u);

    if ((pt[pt_idx] & PTE_PRESENT) == 0)
        return false;

    *pte_out = pt[pt_idx];
    return true;
}

bool paging_user_range_ok(uintptr_t va, size_t len, int for_write)
{
    uintptr_t end;

    if (len == 0)
        return true;

    end = va + len;

    if (end < va || end == 0)
        return false;

    --end;

    for (uintptr_t pg = va & ~0xFFFu; pg <= (end & ~0xFFFu); pg += 4096u) {
        uint32_t pte;

        if (!pte_fetch_pd(paging_get_cr3(), pg, &pte))
            return false;

        if ((pte & PTE_US) == 0)
            return false;

        if (for_write && (pte & PTE_RW) == 0)
            return false;
    }

    return true;
}

uintptr_t paging_clone_kernel_pd(void)
{
    uintptr_t n;
    size_t i;

    n = pmm_alloc_frame();

    if (n == 0)
        return 0;

    {
        uint32_t *dst = (uint32_t *)(uintptr_t)n;
        uint32_t *src = boot_pd;

        for (i = 0; i < 1024u; ++i)
            dst[i] = src[i];
    }

    return n;
}

bool paging_map_page(uintptr_t pd_phys, uintptr_t virt, uintptr_t phys,
    uint32_t pte_flags)
{
    uint32_t *pd = (uint32_t *)(uintptr_t)(pd_phys & 0xFFFFF000u);
    unsigned pd_idx = (unsigned)((uintptr_t)virt >> 22);
    unsigned pt_idx = ((unsigned)((uintptr_t)virt >> 12)) & 0x3FFu;
    uint32_t pde = pd[pd_idx];
    uintptr_t pt_phys;
    uint32_t *pt;
    unsigned j;

    if ((pde & PTE_PRESENT) == 0) {
        pt_phys = pmm_alloc_frame();

        if (pt_phys == 0)
            return false;

        pt = (uint32_t *)(uintptr_t)pt_phys;

        for (j = 0; j < 1024u; ++j)
            pt[j] = 0;

        pd[pd_idx] =
            ((uint32_t)pt_phys & 0xFFFFF000u) | PTE_PRESENT | PTE_RW;
    } else {
        pt_phys = (uintptr_t)(pde & 0xFFFFF000u);
        pt = (uint32_t *)(uintptr_t)pt_phys;
    }

    pt[pt_idx] =
        ((uint32_t)(uintptr_t)phys & 0xFFFFF000u) | (pte_flags & 0xFFFu);

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");

    return true;
}

bool paging_pte_set_user(uintptr_t virt, bool user)
{
    uint32_t pd_idx = (uint32_t)((uintptr_t)virt >> 22);
    uint32_t pt_idx = ((uint32_t)((uintptr_t)virt >> 12)) & 0x3FFu;
    uint32_t pd_e = boot_pd[pd_idx];
    uint32_t *pt;

    if ((pd_e & PTE_PRESENT) == 0)
        return false;

    pt = (uint32_t *)(uintptr_t)(pd_e & 0xFFFFF000u);

    if ((pt[pt_idx] & PTE_PRESENT) == 0)
        return false;

    if (user)
        pt[pt_idx] |= PTE_US;
    else
        pt[pt_idx] &= ~PTE_US;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");

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
