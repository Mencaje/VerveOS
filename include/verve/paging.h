#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool paging_identity_init(uint32_t max_pfn);

bool paging_map_identity_page(uintptr_t phys);

/*
 * OR in PTE User/Supervisor so CPL=3 may access the page (Ring 3 read/write per R/W).
 * Identity mapping must already cover `virt`.
 */
bool paging_pte_set_user(uintptr_t virt, bool user);

/*
 * Each page overlapping [va, va+len) must be present, user (PTE_US), and for
 * writes also PTE_RW. Used to validate syscall pointers from Ring 3.
 */
bool paging_user_range_ok(uintptr_t va, size_t len, int for_write);

uintptr_t paging_get_cr3(void);

void paging_switch_cr3(uintptr_t pd_phys);

/*
 * Allocates one frame and copies the boot page directory (shared PTE pages).
 */
uintptr_t paging_clone_kernel_pd(void);

/*
 * Install mapping in an arbitrary PD (allocates PT if needed). pte_flags should
 * include PTE_PRESENT and desired R/W/U bits (see arch/i386/paging.c).
 */
bool paging_map_page(uintptr_t pd_phys, uintptr_t virt, uintptr_t phys,
    uint32_t pte_flags);

#if defined(__x86_64__)
/*
 * Identity-map [0, max_phys_exclusive) with 2 MiB pages; loads CR3.
 * max_phys_exclusive must be > 0 and fit in the static PD pool (see paging_amd64.c).
 */
bool paging_amd64_identity_init(uint64_t max_phys_exclusive);
#endif
