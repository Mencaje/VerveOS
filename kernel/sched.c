#include <verve/sched.h>
#include <verve/serial.h>
#include <verve/smp.h>

#include <stdint.h>

/*
 * Local APIC periodic timer (vector 64) calls sched_timer_tick() on each CPU.
 * Quantum expiry sets per-CPU need_resched and enqueues on that CPU's defer ring.
 * cpu_switch never runs inside the IRQ handler; sched_process_deferred() drains the
 * ring in thread context (typically via sched_checkpoint).
 */

#define PREEMPT_INTERVAL_TICKS 5u

#define SCHED_DEFER_CAP 16u
#define SCHED_MAX_CPU   32u

static volatile uint64_t g_tick[SCHED_MAX_CPU];
static volatile int g_need_resched[SCHED_MAX_CPU];

static uint32_t g_defer_ring[SCHED_MAX_CPU][SCHED_DEFER_CAP];
static volatile uint32_t g_defer_w[SCHED_MAX_CPU];
static volatile uint32_t g_defer_r[SCHED_MAX_CPU];
static volatile uint32_t g_defer_overflow[SCHED_MAX_CPU];

/*
 * Nested counter: while > 0, schedule()/context switch must not occur (critical
 * sections). IRQ still runs; timers set need_resched/defer until a checkpoint runs
 * with preempt enabled again.
 */
static volatile int g_preempt_disable_depth;

static uint32_t sched_cpu_ix(void)
{
    uint32_t c = smp_cpu_index();

    if (c >= SCHED_MAX_CPU)
        c = 0;

    return c;
}

static void defer_push_tick(uint32_t cpu, uint64_t tick)
{
    uint32_t w = g_defer_w[cpu];
    uint32_t r = g_defer_r[cpu];
    uint32_t next = (w + 1u) % SCHED_DEFER_CAP;

    if (next == r) {
        g_defer_overflow[cpu]++;
        return;
    }

    g_defer_ring[cpu][w] = (uint32_t)(tick & 0xFFFFFFFFu);
    g_defer_w[cpu] = next;
}

void sched_init(void)
{
    uint32_t i;

    for (i = 0; i < SCHED_MAX_CPU; ++i) {
        g_tick[i] = 0;
        g_need_resched[i] = 0;
        g_defer_w[i] = 0;
        g_defer_r[i] = 0;
        g_defer_overflow[i] = 0;
    }

    g_preempt_disable_depth = 0;
}

void sched_timer_tick(void)
{
    uint32_t cpu = sched_cpu_ix();
    uint64_t t;

    g_tick[cpu]++;

    t = g_tick[cpu];

    if (t <= 5u && cpu == 0u) {
        serial_puts("[VerveOS] sched: timer tick ");
        serial_put_u64_hex(t);
        serial_puts("\r\n");
    }

    if ((t % PREEMPT_INTERVAL_TICKS) == 0u) {
        g_need_resched[cpu] = 1;
        defer_push_tick(cpu, t);
    }
}

uint32_t sched_process_deferred(void)
{
    uint32_t flags;
    uint32_t drained = 0;
    uint32_t last_tick = 0;
    uint32_t ovf;
    uint32_t cpu = sched_cpu_ix();

    __asm__ volatile(
        "pushf\n\t"
        "cli\n\t"
        "popl %0"
        : "=m"(flags)
        :
        : "memory");

    while (g_defer_r[cpu] != g_defer_w[cpu]) {
        uint32_t r = g_defer_r[cpu];
        uint32_t v = g_defer_ring[cpu][r];

        last_tick = v;
        drained++;
        g_defer_r[cpu] = (r + 1u) % SCHED_DEFER_CAP;
    }

    ovf = g_defer_overflow[cpu];
    g_defer_overflow[cpu] = 0;

    __asm__ volatile(
        "pushl %0\n\t"
        "popf\n\t"
        :
        : "m"(flags)
        : "memory", "cc");

    if (drained == 0 && ovf == 0)
        return 0;

    serial_puts("[VerveOS] sched: deferred ring drained=");
    serial_put_u64_hex((uint64_t)drained);

    if (drained != 0) {
        serial_puts(" last_tick=");
        serial_put_u64_hex((uint64_t)last_tick);
    }

    if (ovf != 0) {
        serial_puts(" dropped=");
        serial_put_u64_hex((uint64_t)ovf);
    }

    serial_puts(" (cpu_switch stays out of IRQ)\r\n");

    return drained;
}

uint64_t sched_tick_count(void)
{
    return g_tick[sched_cpu_ix()];
}

int sched_need_resched(void)
{
    return g_need_resched[sched_cpu_ix()];
}

void sched_clear_resched(void)
{
    g_need_resched[sched_cpu_ix()] = 0;
}

void sched_preempt_disable(void)
{
    g_preempt_disable_depth++;
}

void sched_preempt_enable(void)
{
    if (g_preempt_disable_depth > 0)
        g_preempt_disable_depth--;
}

int sched_preempt_disabled(void)
{
    return g_preempt_disable_depth != 0;
}
