#pragma once

#include <stdint.h>

typedef struct {
    volatile uint32_t lock;
} verve_spinlock_t;

#define VERVE_SPINLOCK_INIT { 0 }

static inline void verve_cpu_relax(void)
{
    __asm__ volatile("rep; nop" ::: "memory");
}

static inline uint32_t verve_spin_load(const verve_spinlock_t *s)
{
    return s->lock;
}

static inline void verve_spin_lock(verve_spinlock_t *s)
{
    for (;;) {
        uint32_t prev;

        __asm__ volatile(
            "lock xchgl %0, %1"
            : "=r"(prev), "+m"(s->lock)
            : "0"(1u)
            : "memory");

        if (prev == 0)
            return;

        while (verve_spin_load(s) != 0)
            verve_cpu_relax();
    }
}

static inline void verve_spin_unlock(verve_spinlock_t *s)
{
    __sync_lock_release(&s->lock);
}
