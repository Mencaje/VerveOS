#include <verve/paging.h>
#include <verve/proc.h>
#include <verve/serial.h>
#include <verve/syscall.h>
#include <verve/vfs_ram.h>

#include <stddef.h>
#include <stdint.h>

#define SYS_WRITE_MAX 512u

static int32_t sys_write(uint32_t fd, uint32_t buf_va, uint32_t len)
{
    uint32_t i;

    if (fd != 1u)
        return -5; /* EIO */

    if (len > SYS_WRITE_MAX)
        return -22; /* EINVAL */

    if (!paging_user_range_ok((uintptr_t)buf_va, (size_t)len, 0))
        return -14; /* EFAULT */

    for (i = 0; i < len; ++i) {
        char c = *(const volatile char *)(uintptr_t)(buf_va + i);

        serial_putc(c);
    }

    return (int32_t)len;
}

static __attribute__((noreturn)) void sys_exit(uint32_t code)
{
    serial_puts("[VerveOS] SYS_EXIT code=");
    serial_put_u64_hex((uint64_t)code);
    serial_puts("\r\n");

    __asm__ volatile("cli");

    for (;;)
        __asm__ volatile("hlt");
}

void syscall_invoke(verve_exc_frame *f)
{
    uint32_t n = f->eax;
    int32_t out;

    switch (n) {
    case SYS_GETPID:
        out = (int32_t)verve_proc_current_pid();
        break;

    case SYS_WRITE:
        out = sys_write(f->ebx, f->ecx, f->edx);
        break;

    case SYS_EXIT:
        sys_exit(f->ebx);
        break;

    case SYS_OPEN:
        out = verve_vfs_open_u(f->ebx);
        break;

    case SYS_READ:
        out = verve_vfs_read_u((int32_t)f->ebx, f->ecx, f->edx);
        break;

    case SYS_CLOSE:
        verve_vfs_close_u((int32_t)f->ebx);
        out = 0;
        break;

    case SYS_BRK:
        out = verve_proc_sys_brk(f->ebx);
        break;

    default:
        out = -SYS_ENOSYS;
        break;
    }

    f->eax = (uint32_t)out;
}
