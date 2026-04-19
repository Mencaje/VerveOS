#pragma once

#include <stdint.h>

typedef struct {
    uint32_t pid;
    uintptr_t pd_phys;
} verve_proc_t;

void verve_proc_init_singleton(void);

uint32_t verve_proc_current_pid(void);

uintptr_t verve_proc_current_pd_phys(void);

/*
 * Linux-like brk: addr==0 returns current break; otherwise sets the program
 * break (exclusive end). Non-zero addr must be page-aligned. Heap grows
 * upward from 0x80002000 (see proc.c; matches user_bringup stack top).
 */
int32_t verve_proc_sys_brk(uint32_t addr);

void verve_proc_exec_reset_brk(void);
