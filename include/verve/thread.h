#pragma once

void thread_system_init(void);

void thread_spawn(void (*fn)(void));

void sched_yield(void);

void schedule(void);

void sched_checkpoint(void);

void thread_demo_run(void);
