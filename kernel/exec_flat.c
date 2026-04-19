#include <verve/exec_flat.h>
#include <verve/paging.h>
#include <verve/pmm.h>
#include <verve/proc.h>
#include <verve/serial.h>
#include <verve/user.h>
#include <verve/vfs_ram.h>

#include <stddef.h>
#include <stdint.h>

/* Match kernel/user_bringup.c */
#define USER_TEXT_VA    0x80000000u
#define USER_STACK_BASE 0x80001000u
#define USER_STACK_TOP  0x80002000u
#define PTE_USER_RWU    0x7u

static uint32_t g_user_ring0_stack[512] __attribute__((aligned(16)));

static void bzero_user_text(uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i)
        p[i] = 0;
}

static void copy_blob(uint8_t *dst, const uint8_t *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; ++i)
        dst[i] = src[i];
}

void verve_exec_flat_run(const char *path)
{
    const uint8_t *blob;
    uint32_t len;
    uintptr_t pd;
    uintptr_t phys_code;
    uintptr_t phys_stack;
    uint32_t npg;
    uint32_t i;

    if (verve_vfs_get_file_k(path, &blob, &len) != 0 || len == 0u) {
        serial_puts("[VerveOS] exec_flat: ramfs lookup failed for ");
        serial_puts(path);
        serial_puts("\r\n");
        return;
    }

    serial_puts("[VerveOS] exec_flat: load ");
    serial_puts(path);
    serial_puts(" len=");
    serial_put_u64_hex((uint64_t)len);
    serial_puts("\r\n");

    verve_proc_init_singleton();
    verve_proc_exec_reset_brk();

    pd = verve_proc_current_pd_phys();

    if (pd == 0) {
        serial_puts("[VerveOS] exec_flat: no page directory\r\n");
        return;
    }

    phys_code = pmm_alloc_frame();
    phys_stack = pmm_alloc_frame();

    if (phys_code == 0 || phys_stack == 0) {
        serial_puts("[VerveOS] exec_flat: pmm_alloc_frame failed\r\n");
        return;
    }

    npg = (len + 4096u - 1u) / 4096u;

    if (!paging_map_page(pd, (uintptr_t)USER_TEXT_VA, phys_code, PTE_USER_RWU)
        || !paging_map_page(pd, (uintptr_t)USER_STACK_BASE, phys_stack,
            PTE_USER_RWU)) {
        serial_puts("[VerveOS] exec_flat: paging_map_page failed\r\n");
        return;
    }

    for (i = 1u; i < npg; ++i) {
        uintptr_t fr = pmm_alloc_frame();
        uintptr_t va = (uintptr_t)(USER_TEXT_VA + i * 4096u);

        if (fr == 0
            || !paging_map_page(pd, va, fr, PTE_USER_RWU)) {
            serial_puts("[VerveOS] exec_flat: extra text page failed\r\n");
            return;
        }
    }

    paging_switch_cr3(pd);

    bzero_user_text((uint8_t *)(uintptr_t)USER_TEXT_VA,
        (size_t)(npg * 4096u));
    copy_blob((uint8_t *)(uintptr_t)USER_TEXT_VA, blob, (size_t)len);

    tss_set_esp0((uint32_t)(uintptr_t)(g_user_ring0_stack + 512));

    serial_puts("[VerveOS] exec_flat: paging_switch_cr3 + iret\r\n");

    __asm__ volatile("cli");

    verve_enter_user(USER_TEXT_VA, USER_STACK_TOP);

    serial_puts("[VerveOS] exec_flat: unexpected return\r\n");
}
