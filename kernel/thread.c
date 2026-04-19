#include <verve/heap.h>
#include <verve/sched.h>
#include <verve/serial.h>
#include <verve/spinlock.h>
#include <verve/thread.h>

#include <stddef.h>
#include <stdint.h>

#define MAX_THREADS 8u
#define STACK_SIZE    4096u

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
};

extern void cpu_switch(uint32_t *old_sp_save, uint32_t new_sp);

static struct thread threads[MAX_THREADS];
static struct thread main_thread;
static struct thread *current_thread;

static verve_spinlock_t g_thread_rq_lock = VERVE_SPINLOCK_INIT;

static struct thread *g_yield_from;

static void thread_trampoline(void);

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
    struct thread *t = cur->next;

    for (;;) {
        if (t->state == THREAD_RUNNABLE)
            return t;

        if (t == cur)
            return cur;

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

void thread_system_init(void)
{
    for (size_t i = 0; i < MAX_THREADS; ++i) {
        threads[i].esp = 0;
        threads[i].next = NULL;
        threads[i].fn = NULL;
        threads[i].state = THREAD_UNUSED;
        threads[i].kstack = NULL;
    }

    main_thread.esp = 0;
    main_thread.next = &main_thread;
    main_thread.fn = NULL;
    main_thread.state = THREAD_RUNNABLE;
    main_thread.kstack = NULL;

    current_thread = &main_thread;
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
    setup_initial_stack(t);

    enqueue_thread(t);
}

static void thread_trampoline(void)
{
    struct thread *self = current_thread;

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
    struct thread *old;
    struct thread *next;

    if (sched_preempt_disabled())
        return;

    __asm__ volatile(
        "pushf\n\t"
        "cli\n\t"
        "pop %0"
        : "=r"(flags)
        :
        : "memory");

    verve_spin_lock(&g_thread_rq_lock);

    old = current_thread;
    next = pick_next(old);

    if (!next || next == old) {
        verve_spin_unlock(&g_thread_rq_lock);
        __asm__ volatile(
            "push %0\n\t"
            "popf\n\t"
            :
            : "r"(flags)
            : "memory", "cc");
        return;
    }

    current_thread = next;
    g_yield_from = old;

    verve_spin_unlock(&g_thread_rq_lock);

    cpu_switch(&old->esp, next->esp);

    thread_reap_yield_from();

    __asm__ volatile(
        "push %0\n\t"
        "popf\n\t"
        :
        : "r"(flags)
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
    thread_system_init();

    serial_puts("[VerveOS] coop threads: spawning A,B (IRQ0 sets need_resched)\r\n");
    serial_puts("[VerveOS] sched: nested preempt_disable/enable in sched.h\r\n");
    serial_puts("[VerveOS] thread: runqueue guarded by verve_spinlock (spinlock.h)\r\n");

    thread_spawn(worker_a);
    thread_spawn(worker_b);

    serial_puts("[VerveOS] main: round-robin + timer checkpoints\r\n");

    for (size_t i = 0; i < 48; ++i) {
        sched_yield();
        sched_checkpoint();
    }

    serial_puts("[VerveOS] coop demo: main returns to idle\r\n");
}
