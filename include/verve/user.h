#pragma once

#include <stdint.h>

void tss_set_esp0(uint32_t esp);

void verve_enter_user(uint32_t user_eip, uint32_t user_esp);

/*
 * Phase-1 smoke test: map user pages, switch to Ring 3, user hits int 0x80.
 */
void verve_user_bringup_smoke(void);
