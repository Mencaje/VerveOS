#include <verve/paging.h>
#include <verve/pmm.h>
#include <verve/proc.h>
#include <verve/serial.h>
#include <verve/syscall.h>
#include <verve/user.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * User VA outside low RAM identity (typical QEMU): 2 GiB.
 * Two pages: text @ USER_TEXT_VA, stack page @ USER_STACK_BASE.
 */
#define USER_TEXT_VA    0x80000000u
#define USER_STACK_BASE 0x80001000u
#define USER_STACK_TOP  0x80002000u

/* Code 0..USER_DATA_START-1; ro strings in same user page below stack */
#define USER_DATA_START 512u
#define USER_PATH_OFF   (USER_DATA_START)
#define USER_MSG_OFF    544u

/* First heap byte after stack top; brk(0x80003000) maps [0x80002000,0x80003000). */
#define USER_HEAP_BUF_VA 0x80002000u
#define USER_BRK_TARGET  0x80003000u
#define USER_READ_MAX    128u

#define PTE_USER_RWU (0x7u) /* PRESENT | RW | US */

static uint32_t g_user_ring0_stack[512] __attribute__((aligned(16)));

static size_t emit_user_phase3(uint8_t *code, uint32_t brk_goal, uint32_t path_va,
    uint32_t buf_va, uint32_t msg_va, uint32_t msg_len)
{
    uint8_t *q = code;

    /* SYS_BRK: map heap pages up to brk_goal (before touch buf_va on heap). */
    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_BRK}, 4);
    q += 4;
    *q++ = 0xBB;
    memcpy(q, &brk_goal, 4);
    q += 4;
    *q++ = 0xCD;
    *q++ = 0x80;

    /* SYS_OPEN: eax=path -> fd in eax */
    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_OPEN}, 4);
    q += 4;
    *q++ = 0xBB;
    memcpy(q, &path_va, 4);
    q += 4;
    *q++ = 0xCD;
    *q++ = 0x80;
    *q++ = 0x89;
    *q++ = 0xC6; /* mov esi, eax */

    /* SYS_READ: ebx=fd, ecx=buf, edx=len */
    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_READ}, 4);
    q += 4;
    *q++ = 0x89;
    *q++ = 0xF3; /* mov ebx, esi */
    *q++ = 0xB9;
    memcpy(q, &buf_va, 4);
    q += 4;
    *q++ = 0xBA;
    memcpy(q, &(uint32_t){USER_READ_MAX}, 4);
    q += 4;
    *q++ = 0xCD;
    *q++ = 0x80;
    *q++ = 0x89;
    *q++ = 0xC7; /* mov edi, eax (nread) */

    /* SYS_WRITE: stdout, buffer, nread */
    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_WRITE}, 4);
    q += 4;
    *q++ = 0xBB;
    memcpy(q, &(uint32_t){1u}, 4);
    q += 4;
    *q++ = 0xB9;
    memcpy(q, &buf_va, 4);
    q += 4;
    *q++ = 0x89;
    *q++ = 0xFA; /* mov edx, edi */
    *q++ = 0xCD;
    *q++ = 0x80;

    /* SYS_CLOSE */
    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_CLOSE}, 4);
    q += 4;
    *q++ = 0x89;
    *q++ = 0xF3;
    *q++ = 0xCD;
    *q++ = 0x80;

    /* Banner line */
    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_WRITE}, 4);
    q += 4;
    *q++ = 0xBB;
    memcpy(q, &(uint32_t){1u}, 4);
    q += 4;
    *q++ = 0xB9;
    memcpy(q, &msg_va, 4);
    q += 4;
    *q++ = 0xBA;
    memcpy(q, &msg_len, 4);
    q += 4;
    *q++ = 0xCD;
    *q++ = 0x80;

    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_GETPID}, 4);
    q += 4;
    *q++ = 0xCD;
    *q++ = 0x80;

    *q++ = 0xB8;
    memcpy(q, &(uint32_t){SYS_EXIT}, 4);
    q += 4;
    *q++ = 0xBB;
    memcpy(q, &(uint32_t){0u}, 4);
    q += 4;
    *q++ = 0xCD;
    *q++ = 0x80;

    return (size_t)(q - code);
}

void verve_user_bringup_smoke(void)
{
    uintptr_t phys_code;
    uintptr_t phys_stack;
    uintptr_t pd;
    uint8_t *code;
    uint32_t msg_va;
    uint32_t path_va;
    static const char msg[] =
        "VerveOS user: banner after /hello.txt (same PD @ 2GiB).\r\n";
    static const char path[] = "/hello.txt";
    uint32_t msg_len;
    size_t nbytes;

    serial_puts("[VerveOS] userspace: ramfs OPEN/READ + WRITE (clone PD + CR3)\r\n");

    verve_proc_init_singleton();
    pd = verve_proc_current_pd_phys();

    if (pd == 0) {
        serial_puts("[VerveOS] userspace: paging_clone_kernel_pd failed\r\n");
        return;
    }

    phys_code = pmm_alloc_frame();
    phys_stack = pmm_alloc_frame();

    if (phys_code == 0 || phys_stack == 0) {
        serial_puts("[VerveOS] userspace smoke: pmm_alloc_frame failed\r\n");
        return;
    }

    if (!paging_map_page(pd, (uintptr_t)USER_TEXT_VA, phys_code,
            PTE_USER_RWU)
        || !paging_map_page(pd, (uintptr_t)USER_STACK_BASE, phys_stack,
            PTE_USER_RWU)) {
        serial_puts("[VerveOS] userspace smoke: paging_map_page failed\r\n");
        return;
    }

    msg_va = USER_TEXT_VA + USER_MSG_OFF;
    path_va = USER_TEXT_VA + USER_PATH_OFF;
    msg_len = (uint32_t)(sizeof(msg) - 1u);

    code = (uint8_t *)(uintptr_t)phys_code;

    nbytes = emit_user_phase3(code, USER_BRK_TARGET, path_va, USER_HEAP_BUF_VA,
        msg_va, msg_len);

    if (nbytes >= USER_DATA_START) {
        serial_puts("[VerveOS] userspace smoke: code too large\r\n");
        return;
    }

    memcpy(code + USER_MSG_OFF, msg, (size_t)msg_len);
    memcpy(code + USER_PATH_OFF, path, sizeof(path));

    tss_set_esp0((uint32_t)(uintptr_t)(g_user_ring0_stack + 512));

    serial_puts("[VerveOS] userspace: paging_switch_cr3(proc) + iret (Ring 3)\r\n");

    __asm__ volatile("cli");

    paging_switch_cr3(pd);

    verve_enter_user(USER_TEXT_VA, USER_STACK_TOP);

    serial_puts("[VerveOS] userspace smoke: unexpected return\r\n");
}
