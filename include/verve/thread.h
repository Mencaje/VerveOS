#pragma once

#include <stdint.h>

void thread_subsystem_early_init(uint32_t logical_cpus);

void thread_system_init(void);

void thread_spawn(void (*fn)(void));

void sched_yield(void);

void schedule(void);

void sched_checkpoint(void);

void thread_ap_online(void);

void thread_demo_run(void);
