#pragma once

#include <stdint.h>

#include <verve/mboot2.h>

uint32_t acpi_collect_cpu_apic_ids(const struct mb2_fixed *info,
    uint8_t *apic_ids_out, uint32_t max_ids);
