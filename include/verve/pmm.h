#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <verve/mboot2.h>

bool pmm_init(const struct mb2_fixed *info, uint32_t mb_info_phys);

uint32_t pmm_frame_count(void);

uint32_t pmm_max_pfn(void);

uintptr_t pmm_alloc_frame(void);

void pmm_free_frame(uintptr_t phys);
