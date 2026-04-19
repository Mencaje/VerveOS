#include <verve/paging.h>
#include <verve/pmm.h>
#include <verve/proc.h>

#include <stdint.h>

/*
 * Same VA layout as kernel/user_bringup.c: stack covers [0x80001000,0x80002000).
 * Heap grows at higher VA starting at USER_STACK_TOP (exclusive end of stack).
 */
#define VERVE_PAGE_SIZE      4096u
#define VERVE_USER_HEAP_BASE 0x80002000u
#define VERVE_USER_HEAP_CAP  0x80400000u
#define PTE_USER_RWU         0x7u /* PRESENT | RW | US */

static verve_proc_t g_cur;
static uint32_t g_brk;

void verve_proc_init_singleton(void)
{
    uintptr_t pd = paging_clone_kernel_pd();

    g_cur.pid = 1u;
    g_cur.pd_phys = pd;
    g_brk = VERVE_USER_HEAP_BASE;
}

uint32_t verve_proc_current_pid(void)
{
    return g_cur.pid;
}

uintptr_t verve_proc_current_pd_phys(void)
{
    return g_cur.pd_phys;
}

void verve_proc_exec_reset_brk(void)
{
    g_brk = VERVE_USER_HEAP_BASE;
}

int32_t verve_proc_sys_brk(uint32_t addr)
{
    uintptr_t pd = g_cur.pd_phys;
    uint32_t cur = g_brk;

    if (pd == 0)
        return -12; /* ENOMEM */

    if (addr == 0)
        return (int32_t)cur;

    /* Only page-aligned breaks: keeps the map loop one page per step. */
    if ((addr & (VERVE_PAGE_SIZE - 1u)) != 0u)
        return -22; /* EINVAL */

    if (addr < VERVE_USER_HEAP_BASE || addr >= VERVE_USER_HEAP_CAP)
        return -22; /* EINVAL */

    if (addr < cur)
        return -22;

    if (addr == cur)
        return (int32_t)cur;

    {
        uint32_t va;

        for (va = cur; va < addr; va += VERVE_PAGE_SIZE) {
            uintptr_t frame = pmm_alloc_frame();

            if (frame == 0)
                return -12;

            if (!paging_map_page(pd, va, frame, PTE_USER_RWU)) {
                pmm_free_frame(frame);
                return -12;
            }
        }
    }

    g_brk = addr;
    return (int32_t)addr;
}
