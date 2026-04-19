#include <arch/i386/io.h>
#include <stddef.h>

#define COM1_DATA 0x03F8u

void serial_putc(char c)
{
    while ((verve_inb(COM1_DATA + 5) & 0x20u) == 0)
        ;

    verve_outb(COM1_DATA, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void serial_put_u64_hex(uint64_t v)
{
    static const char *xd = "0123456789ABCDEF";
    char buf[32];
    size_t i = 0;
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
    serial_puts(buf);
}
