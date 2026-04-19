#include <verve/sched.h>
#include <verve/serial.h>

#include <stdint.h>

/*
 * IRQ0 drives sched_timer_tick().
 * Quantum expiry sets sched_need_resched() and also enqueues a deferred record.
 * cpu_switch never runs inside the IRQ handler; sched_process_deferred() drains the
 * ring in thread context (typically via sched_checkpoint).
 */

#define PREEMPT_INTERVAL_TICKS 5u

#define SCHED_DEFER_CAP 16u

static volatile uint64_t g_tick;
static volatile int g_need_resched;

static uint32_t g_defer_ring[SCHED_DEFER_CAP];
static volatile uint32_t g_defer_w;
static volatile uint32_t g_defer_r;
static volatile uint32_t g_defer_overflow;

/*
 * Nested counter: while > 0, schedule()/context switch must not occur (critical
 * sections). IRQ still runs; timers set need_resched/defer until a checkpoint runs
 * with preempt enabled again.
 */
static volatile int g_preempt_disable_depth;

static void defer_push_tick(uint64_t tick)
{
    uint32_t w = g_defer_w;
    uint32_t r = g_defer_r;
    uint32_t next = (w + 1u) % SCHED_DEFER_CAP;

    if (next == r) {
        g_defer_overflow++;
        return;
    }

    g_defer_ring[w] = (uint32_t)(tick & 0xFFFFFFFFu);
    g_defer_w = next;
}

void sched_init(void)
{
    g_tick = 0;
    g_need_resched = 0;

    g_defer_w = 0;
    g_defer_r = 0;
    g_defer_overflow = 0;

    g_preempt_disable_depth = 0;
}

void sched_timer_tick(void)
{
    uint64_t t;

    g_tick++;

    t = g_tick;

    if (t <= 5u) {
        serial_puts("[VerveOS] sched: timer tick ");
        serial_put_u64_hex(t);
        serial_puts("\r\n");
    }

    if ((t % PREEMPT_INTERVAL_TICKS) == 0u) {
        g_need_resched = 1;
        defer_push_tick(t);
    }
}

uint32_t sched_process_deferred(void)
{
    uint32_t flags;
    uint32_t drained = 0;
    uint32_t last_tick = 0;
    uint32_t ovf;

    __asm__ volatile(
        "pushf\n\t"
        "cli\n\t"
        "pop %0"
        : "=r"(flags)
        :
        : "memory");

    while (g_defer_r != g_defer_w) {
        uint32_t r = g_defer_r;
        uint32_t v = g_defer_ring[r];

        last_tick = v;
        drained++;
        g_defer_r = (r + 1u) % SCHED_DEFER_CAP;
    }

    ovf = g_defer_overflow;
    g_defer_overflow = 0;

    __asm__ volatile(
        "push %0\n\t"
        "popf\n\t"
        :
        : "r"(flags)
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
    return g_tick;
}

int sched_need_resched(void)
{
    return g_need_resched;
}

void sched_clear_resched(void)
{
    g_need_resched = 0;
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
