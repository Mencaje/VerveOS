#include <arch/i386/io.h>
#include <arch/i386/pic.h>

#define PIC1_CMD  0x20u
#define PIC1_DATA 0x21u
#define PIC2_CMD  0xA0u
#define PIC2_DATA 0xA1u

static void pic_wait(void)
{
    verve_inb(PIC1_CMD);
}

void pic_init(uint8_t master_vector_base, uint8_t slave_vector_base)
{
    verve_outb(PIC1_CMD, 0x11u);
    pic_wait();
    verve_outb(PIC2_CMD, 0x11u);
    pic_wait();

    verve_outb(PIC1_DATA, master_vector_base);
    pic_wait();
    verve_outb(PIC2_DATA, slave_vector_base);
    pic_wait();

    verve_outb(PIC1_DATA, 4u);
    pic_wait();
    verve_outb(PIC2_DATA, 2u);
    pic_wait();

    verve_outb(PIC1_DATA, 1u);
    pic_wait();
    verve_outb(PIC2_DATA, 1u);
    pic_wait();

    verve_outb(PIC1_DATA, 0xFFu);
    verve_outb(PIC2_DATA, 0xFFu);
}

void pic_irq_mask_all(void)
{
    verve_outb(PIC1_DATA, 0xFFu);
    verve_outb(PIC2_DATA, 0xFFu);
}

void pic_irq_unmask(uint8_t irq_line)
{
    uint16_t port = irq_line < 8u ? PIC1_DATA : PIC2_DATA;
    uint8_t shift = irq_line < 8u ? (uint8_t)irq_line : (uint8_t)(irq_line - 8u);
    uint8_t val = verve_inb(port);

    val = (uint8_t)(val & (uint8_t)~(1u << shift));
    verve_outb(port, val);
}

void pic_send_eoi(uint8_t irq_line)
{
    if (irq_line >= 8u)
        verve_outb(PIC2_CMD, 0x20u);

    verve_outb(PIC1_CMD, 0x20u);
}
