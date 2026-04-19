#pragma once

#include <stdint.h>

static inline uint8_t verve_inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %w1, %b0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void verve_outb(uint16_t port, uint8_t data)
{
    __asm__ volatile("outb %b0, %w1" : : "a"(data), "Nd"(port));
}
