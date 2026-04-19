#pragma once

#include <stdbool.h>
#include <stdint.h>

bool paging_identity_init(uint32_t max_pfn);

bool paging_map_identity_page(uintptr_t phys);
