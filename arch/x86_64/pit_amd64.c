#include <arch/x86_64/io.h>
#include <arch/x86_64/pit.h>

#define PIT_CH0_DATA 0x40u
#define PIT_CMD      0x43u

void pit_set_frequency_hz(uint32_t hz)
{
    uint32_t divisor = 1193182u / hz;

    if (divisor > 65535u)
        divisor = 65535u;

    if (divisor < 2u)
        divisor = 2u;

    verve_outb(PIT_CMD, 0x36u);

    verve_outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFFu));
    verve_outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFFu));
}
