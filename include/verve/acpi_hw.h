#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <verve/mboot2.h>

bool acpi_hw_init(const struct mb2_fixed *info);

uint32_t acpi_hw_cpu_count(void);

void acpi_hw_copy_apic_ids(uint8_t *out, uint32_t max);

bool acpi_hw_has_ioapic(void);

uint32_t acpi_hw_ioapic_phys(void);

uint32_t acpi_hw_ioapic_gsi_base(void);

uint32_t acpi_hw_irq0_gsi(void);

void acpi_hotplug_stub_init(void);
