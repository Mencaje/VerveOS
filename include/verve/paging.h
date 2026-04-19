#pragma once

#include <stdbool.h>
#include <stdint.h>

bool paging_identity_init(uint32_t max_pfn);

bool paging_map_identity_page(uintptr_t phys);

#if defined(__x86_64__)
/*
 * Identity-map [0, max_phys_exclusive) with 2 MiB pages; loads CR3.
 * max_phys_exclusive must be > 0 and fit in the static PD pool (see paging_amd64.c).
 */
bool paging_amd64_identity_init(uint64_t max_phys_exclusive);
#endif
