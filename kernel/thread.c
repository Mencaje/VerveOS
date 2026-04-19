#include <verve/heap.h>
#include <verve/sched.h>
#include <verve/serial.h>
#include <verve/smp.h>
#include <verve/spinlock.h>
#include <verve/thread.h>

#include <stddef.h>
#include <stdint.h>

#define MAX_THREADS     64u
#define MAX_SMP_CPU     32u
#define STACK_SIZE      4096u
#define AFF_ANY         0xFFFFFFFFu

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_RUNNABLE,
    THREAD_ZOMBIE,
} thread_state_t;

struct thread {
    uint32_t esp;
    struct thread *next;
    void (*fn)(void);
    thread_state_t state;
    uint8_t *kstack;
    uint32_t affinity_cpu;
    int32_t sched_token;
};

extern void cpu_switch(uint32_t *old_sp_save, uint32_t new_sp);

static struct thread threads[MAX_THREADS];
static struct thread main_thread;
static struct thread *g_current[MAX_SMP_CPU];
static struct thread *g_idle_for_cpu[MAX_SMP_CPU];

static verve_spinlock_t g_thread_rq_lock = VERVE_SPINLOCK_INIT;

static struct thread *g_yield_from;

static void thread_trampoline(void);

static struct thread *thread_current_slot(void)
{
    uint32_t ix = smp_cpu_index();

    if (ix >= MAX_SMP_CPU)
        ix = 0;

    return g_current[ix];
}

static void thread_set_current_slot(uint32_t ix, struct thread *t)
{
    if (ix < MAX_SMP_CPU)
        g_current[ix] = t;
}

static void unlink_thread(struct thread *t)
{
    struct thread *p = &main_thread;

    do {
        if (p->next == t) {
            p->next = t->next;
            t->next = NULL;
            return;
        }

        p = p->next;
    } while (p != &main_thread);
}

static void thread_reap_yield_from(void)
{
    struct thread *z = g_yield_from;
    uint8_t *stk;

    g_yield_from = NULL;

    if (!z || z == &main_thread)
        return;

    if (z->state != THREAD_ZOMBIE || !z->kstack)
        return;

    stk = z->kstack;

    verve_spin_lock(&g_thread_rq_lock);
    unlink_thread(z);
    z->state = THREAD_UNUSED;
    z->kstack = NULL;
    verve_spin_unlock(&g_thread_rq_lock);

    kfree(stk);
}

static void enqueue_thread(struct thread *t)
{
    struct thread *h = &main_thread;

    verve_spin_lock(&g_thread_rq_lock);

    t->next = h->next;
    h->next = t;

    verve_spin_unlock(&g_thread_rq_lock);
}

static struct thread *pick_next(struct thread *cur)
{
    uint32_t me = smp_cpu_index();
    struct thread *t = cur->next;

    for (;;) {
        if (t == cur)
            return cur;

        if (t->state == THREAD_RUNNABLE) {
            if (t->affinity_cpu != AFF_ANY && t->affinity_cpu != me) {
                t = t->next;
                continue;
            }

            if (__sync_bool_compare_and_swap(&t->sched_token, -1,
                    (int32_t)me))
                return t;
        }

        t = t->next;
    }
}

static struct thread *thread_alloc_slot(void)
{
    for (size_t i = 0; i < MAX_THREADS; ++i) {
        if (threads[i].state == THREAD_UNUSED)
            return &threads[i];
    }

    return NULL;
}

static void setup_initial_stack(struct thread *t)
{
    uint32_t *sp = (uint32_t *)(t->kstack + STACK_SIZE);

    sp -= 5;
    sp[0] = 0;
    sp[1] = 0;
    sp[2] = 0;
    sp[3] = 0;
    sp[4] = (uint32_t)(uintptr_t)thread_trampoline;

    t->esp = (uint32_t)(uintptr_t)sp;
}

static void ap_idle_fn(void)
{
    for (;;)
        sched_checkpoint();
}

void thread_subsystem_early_init(uint32_t logical_cpus)
{
    uint32_t k;
    uint32_t lim = logical_cpus;

    if (lim == 0)
        lim = 1;

    if (lim > MAX_SMP_CPU)
        lim = MAX_SMP_CPU;

    for (size_t i = 0; i < MAX_THREADS; ++i) {
        threads[i].esp = 0;
        threads[i].next = NULL;
        threads[i].fn = NULL;
        threads[i].state = THREAD_UNUSED;
        threads[i].kstack = NULL;
        threads[i].affinity_cpu = AFF_ANY;
        threads[i].sched_token = -1;
    }

    for (k = 0; k < MAX_SMP_CPU; ++k) {
        g_current[k] = NULL;
        g_idle_for_cpu[k] = NULL;
    }

    main_thread.esp = 0;
    main_thread.next = &main_thread;
    main_thread.fn = NULL;
    main_thread.state = THREAD_RUNNABLE;
    main_thread.kstack = NULL;
    main_thread.affinity_cpu = 0;
    main_thread.sched_token = 0;

    g_current[0] = &main_thread;

    for (k = 1u; k < lim; ++k) {
        struct thread *t = thread_alloc_slot();

        if (!t) {
            serial_puts("[VerveOS] thread: idle pool full\r\n");
            break;
        }

        t->kstack = kmalloc(STACK_SIZE);

        if (!t->kstack) {
            serial_puts("[VerveOS] thread: kmalloc idle stack failed\r\n");
            t->state = THREAD_UNUSED;
            break;
        }

        t->fn = ap_idle_fn;
        t->state = THREAD_RUNNABLE;
        t->affinity_cpu = k;
        t->sched_token = -1;
        setup_initial_stack(t);

        g_idle_for_cpu[k] = t;

        enqueue_thread(t);
    }

    serial_puts("[VerveOS] thread: subsystem early init logical_cpus=");
    serial_put_u64_hex((uint64_t)lim);
    serial_puts("\r\n");
}

void thread_system_init(void)
{
    thread_subsystem_early_init(1);
}

void thread_spawn(void (*fn)(void))
{
    struct thread *t = thread_alloc_slot();

    if (!fn || !t) {
        serial_puts("[VerveOS] thread_spawn: failed (null or pool full)\r\n");
        return;
    }

    t->kstack = kmalloc(STACK_SIZE);

    if (!t->kstack) {
        serial_puts("[VerveOS] thread_spawn: kmalloc(kernel stack) failed\r\n");
        return;
    }

    t->fn = fn;
    t->state = THREAD_RUNNABLE;
    t->affinity_cpu = AFF_ANY;
    t->sched_token = -1;
    setup_initial_stack(t);

    enqueue_thread(t);
}

static void thread_trampoline(void)
{
    struct thread *self = thread_current_slot();

    if (self && self->fn && self != &main_thread)
        self->fn();

    if (self && self != &main_thread) {
        verve_spin_lock(&g_thread_rq_lock);
        self->state = THREAD_ZOMBIE;
        verve_spin_unlock(&g_thread_rq_lock);
    }

    while (1)
        sched_yield();
}

void sched_yield(void)
{
    uint32_t flags;
    uint32_t cpu_ix;
    struct thread *old;
    struct thread *next;

    if (sched_preempt_disabled())
        return;

    __asm__ volatile(
        "pushf\n\t"
        "cli\n\t"
        "popl %0"
        : "=m"(flags)
        :
        : "memory");

    verve_spin_lock(&g_thread_rq_lock);

    cpu_ix = smp_cpu_index();
    old = g_current[cpu_ix];

    next = pick_next(old);

    if (!next || next == old) {
        verve_spin_unlock(&g_thread_rq_lock);
        __asm__ volatile(
            "pushl %0\n\t"
            "popf\n\t"
            :
            : "m"(flags)
            : "memory", "cc");
        return;
    }

    if (old->state == THREAD_RUNNABLE && old != next)
        old->sched_token = -1;

    g_current[cpu_ix] = next;
    g_yield_from = old;

    verve_spin_unlock(&g_thread_rq_lock);

    cpu_switch(&old->esp, next->esp);

    thread_reap_yield_from();

    __asm__ volatile(
        "pushl %0\n\t"
        "popf\n\t"
        :
        : "m"(flags)
        : "memory", "cc");
}

void schedule(void)
{
    if (sched_preempt_disabled())
        return;

    sched_yield();
}

void sched_checkpoint(void)
{
    uint32_t drained = sched_process_deferred();

    if (drained != 0 && !sched_preempt_disabled()) {
        schedule();
        sched_clear_resched();
    }

    if (!sched_need_resched() || sched_preempt_disabled())
        return;

    sched_yield();
    sched_clear_resched();
}

void thread_ap_online(void)
{
    uint32_t ix = smp_cpu_index();

    if (ix == 0 || ix >= MAX_SMP_CPU)
        return;

    if (!g_idle_for_cpu[ix])
        return;

    thread_set_current_slot(ix, g_idle_for_cpu[ix]);

    g_idle_for_cpu[ix]->sched_token = (int32_t)ix;

    serial_puts("[VerveOS] thread: AP sched online cpu=");
    serial_put_u64_hex((uint64_t)ix);
    serial_puts("\r\n");

    __asm__ volatile("sti");

    for (;;) {
        sched_checkpoint();
        sched_yield();
    }
}

static void worker_a(void)
{
    serial_puts("[VerveOS] thread A start (will checkpoint on timer quantum)\r\n");

    for (unsigned i = 0; i < 6; i++) {
        if (i == 0) {
            sched_preempt_disable();
            serial_puts("[VerveOS] thread A critical begin (preempt disabled)\r\n");
            serial_puts("[VerveOS] thread A critical end\r\n");
            sched_preempt_enable();
        }

        serial_puts("[VerveOS] thread A work ");
        serial_put_u64_hex((uint64_t)i);
        serial_puts("\r\n");
        sched_checkpoint();
    }

    serial_puts("[VerveOS] thread A end\r\n");
}

static void worker_b(void)
{
    serial_puts("[VerveOS] thread B start (will checkpoint on timer quantum)\r\n");

    for (unsigned i = 0; i < 6; i++) {
        serial_puts("[VerveOS] thread B work ");
        serial_put_u64_hex((uint64_t)i);
        serial_puts("\r\n");
        sched_checkpoint();
    }

    serial_puts("[VerveOS] thread B end\r\n");
}

void thread_demo_run(void)
{
    serial_puts("[VerveOS] coop threads: spawning A,B (LAPIC timer sets need_resched)\r\n");
    serial_puts("[VerveOS] thread: SMP affinity + sched_token claim\r\n");

    thread_spawn(worker_a);
    thread_spawn(worker_b);

    serial_puts("[VerveOS] main: round-robin + timer checkpoints\r\n");

    for (size_t i = 0; i < 48; ++i) {
        sched_yield();
        sched_checkpoint();
    }

    serial_puts("[VerveOS] coop demo: main returns to idle\r\n");
}
