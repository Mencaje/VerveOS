#pragma once

#include <stdint.h>

void pic_init(uint8_t master_vector_base, uint8_t slave_vector_base);

void pic_irq_unmask(uint8_t irq_line);

void pic_irq_mask_all(void);

void pic_send_eoi(uint8_t irq_line);
