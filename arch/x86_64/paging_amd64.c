/*
 * Long-mode four-level paging: identity map physical RAM using 2 MiB pages (PS in PD).
 * Mirrors early boot_chain32 mapping; installs our own CR3 so later we can remap.
 */
#include <verve/paging.h>

#include <stdint.h>

#define PTE_PRESENT (1ull << 0)
#define PTE_RW      (1ull << 1)
#define PTE_PS      (1ull << 7)

#define PAGE_2M     (1ull << 21)

/* Up to 64 × 1 GiB (each PD covers 512 × 2 MiB) — static pool only for bring-up. */
#define MAX_PD 64u

static uint64_t boot_pml4[512] __attribute__((aligned(4096)));
static uint64_t boot_pdpt[512] __attribute__((aligned(4096)));
static uint64_t boot_pd[MAX_PD][512] __attribute__((aligned(4096)));

static void tbl_zero(uint64_t *p, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; ++i)
        p[i] = 0;
}

bool paging_amd64_identity_init(uint64_t max_phys_exclusive)
{
    uintptr_t pml4_phys = (uintptr_t)&boot_pml4[0];
    uintptr_t pdpt_phys = (uintptr_t)&boot_pdpt[0];
    unsigned pd_idx;

    uint64_t span = max_phys_exclusive;

    if (span == 0 || span > (MAX_PD * (512ull * PAGE_2M)))
        return false;

    uint64_t n2m = (span + PAGE_2M - 1u) >> 21;
    unsigned n_pd = (unsigned)((n2m + 511u) / 512u);

    if (n_pd == 0 || (unsigned)n_pd > MAX_PD)
        return false;

    tbl_zero(boot_pml4, 512u);
    tbl_zero(boot_pdpt, 512u);
    for (pd_idx = 0; pd_idx < (unsigned)MAX_PD; ++pd_idx)
        tbl_zero(boot_pd[pd_idx], 512u);

    boot_pml4[0] = (uint64_t)pdpt_phys | PTE_PRESENT | PTE_RW;

    for (pd_idx = 0; pd_idx < n_pd; ++pd_idx) {
        uintptr_t pd_phys = (uintptr_t)&boot_pd[pd_idx][0];

        boot_pdpt[pd_idx] = (uint64_t)pd_phys | PTE_PRESENT | PTE_RW;

        for (unsigned j = 0; j < 512u; ++j) {
            uint64_t glob = (uint64_t)pd_idx * 512u + (uint64_t)j;

            if (glob >= n2m)
                break;

            boot_pd[pd_idx][j] =
                (glob << 21) | PTE_PRESENT | PTE_RW | PTE_PS;
        }
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");

    return true;
}
