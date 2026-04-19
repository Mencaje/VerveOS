#pragma once

#include <stdint.h>

void sched_init(void);

void sched_timer_tick(void);

uint64_t sched_tick_count(void);

int sched_need_resched(void);

void sched_clear_resched(void);

uint32_t sched_process_deferred(void);

void sched_preempt_disable(void);

void sched_preempt_enable(void);

int sched_preempt_disabled(void);
