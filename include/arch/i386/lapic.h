#pragma once

#include <stdint.h>

#define LAPIC_DEFAULT_BASE 0xFEE00000u

void lapic_msr_enable(void);

void lapic_mmio_map(void);

void lapic_software_enable(void);

void lapic_eoi(void);

uint8_t lapic_id(void);

void lapic_send_init(uint8_t apic_id);

void lapic_send_sipi(uint8_t apic_id, uint8_t vector);

void lapic_icr_wait(void);

void lapic_bsp_early_init(void);

void lapic_timer_periodic(uint8_t vector, uint32_t initial_count, uint8_t divide_reg);
