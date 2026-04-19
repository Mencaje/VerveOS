#pragma once

#include <stdint.h>

void serial_putc(char c);
void serial_puts(const char *s);
void serial_put_u64_hex(uint64_t v);
