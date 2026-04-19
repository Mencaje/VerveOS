#include <arch/i386/io.h>
#include <verve/spinlock.h>

#include <stddef.h>
#include <stdint.h>

#define COM1_DATA 0x03F8u

static verve_spinlock_t g_serial_lock = VERVE_SPINLOCK_INIT;

static void serial_putc_raw(char c)
{
    while ((verve_inb(COM1_DATA + 5) & 0x20u) == 0)
        ;

    verve_outb(COM1_DATA, (uint8_t)c);
}

void serial_putc(char c)
{
    verve_spin_lock(&g_serial_lock);
    serial_putc_raw(c);
    verve_spin_unlock(&g_serial_lock);
}

void serial_puts(const char *s)
{
    verve_spin_lock(&g_serial_lock);

    while (*s)
        serial_putc_raw(*s++);

    verve_spin_unlock(&g_serial_lock);
}

void serial_put_u64_hex(uint64_t v)
{
    static const char *xd = "0123456789ABCDEF";
    char buf[32];
    size_t i = 0;

    verve_spin_lock(&g_serial_lock);

    buf[i++] = '0';
    buf[i++] = 'x';

    int started = 0;
    for (int s = 60; s >= 0; s -= 4) {
        unsigned nibble = (unsigned)((v >> s) & 0xFULL);

        if (started == 0 && nibble == 0 && s != 0)
            continue;

        started = 1;
        buf[i++] = xd[nibble];
        if (i >= sizeof(buf) - 1)
            break;
    }

    if (started == 0)
        buf[i++] = '0';

    buf[i] = 0;

    {
        const char *p = buf;

        while (*p)
            serial_putc_raw(*p++);
    }

    verve_spin_unlock(&g_serial_lock);
}
