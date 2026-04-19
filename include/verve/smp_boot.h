#pragma once

#include <stdint.h>

#define VERVE_SMP_TRAMP_PHYS 0x8000u
#define VERVE_SMP_MAIL_PHYS  0x9000u

struct verve_smp_mailbox {
    uint32_t cr3_phys;
    uint32_t esp;
    uint32_t entry;
};
