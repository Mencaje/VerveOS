#pragma once

#include <stdint.h>

#include <verve/mboot2.h>

void smp_bootstrap_apic_map(void);

uint32_t smp_cpu_index(void);

void smp_bsp_timer_start(void);

void smp_init(const struct mb2_fixed *info);

uint32_t smp_cpu_count(void);
