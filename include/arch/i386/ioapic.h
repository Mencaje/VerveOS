#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ioapic_init_from_acpi(void);

void ioapic_route_irq0_timer(uint8_t bsp_apic_id);
