#include <verve/interrupt.h>

#include <stddef.h>
#include <stdint.h>

extern void gdt_flush(void);

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_hi;
} __attribute__((packed));

static struct gdt_entry gdt_desc[5];

struct gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static void gdt_set_gate(size_t i, uint32_t base, uint32_t limit, uint8_t access,
    uint8_t gran)
{
    gdt_desc[i].base_lo = (uint16_t)(base & 0xFFFFu);
    gdt_desc[i].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt_desc[i].base_hi = (uint8_t)((base >> 24) & 0xFFu);

    gdt_desc[i].limit_lo = (uint16_t)(limit & 0xFFFFu);
    gdt_desc[i].gran = (uint8_t)(((limit >> 16) & 0x0Fu) | (gran & 0xF0u));

    gdt_desc[i].access = access;
}

void gdt_init(void)
{
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);

    struct gdtr gdtr;

    gdtr.limit = (uint16_t)(sizeof(gdt_desc) - 1u);
    gdtr.base = (uint32_t)(uintptr_t)gdt_desc;

    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    gdt_flush();
}
