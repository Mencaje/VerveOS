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

/*
 * 0 null, 1 kcode 0x08, 2 kdata 0x10, 3 ucode -> 0x1B, 4 udata -> 0x23,
 * 5 TSS -> 0x28
 */
static struct gdt_entry gdt_desc[6];

struct gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_i386 {
    uint16_t link;
    uint16_t reserved0;
    uint32_t esp0;
    uint16_t ss0;
    uint16_t reserved1;
    uint32_t esp1;
    uint16_t ss1;
    uint16_t reserved2;
    uint32_t esp2;
    uint16_t ss2;
    uint16_t reserved3;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint16_t es;
    uint16_t reserved4;
    uint16_t cs;
    uint16_t reserved5;
    uint16_t ss;
    uint16_t reserved6;
    uint16_t ds;
    uint16_t reserved7;
    uint16_t fs;
    uint16_t reserved8;
    uint16_t gs;
    uint16_t reserved9;
    uint16_t ldtr;
    uint16_t reserved10;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss_i386 g_tss __attribute__((aligned(16)));

static uint32_t g_null_kstack[512] __attribute__((aligned(16)));

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

static void gdt_set_tss(size_t i, uint32_t base, uint32_t limit)
{
    gdt_desc[i].base_lo = (uint16_t)(base & 0xFFFFu);
    gdt_desc[i].base_mid = (uint8_t)((base >> 16) & 0xFFu);
    gdt_desc[i].base_hi = (uint8_t)((base >> 24) & 0xFFu);

    gdt_desc[i].limit_lo = (uint16_t)(limit & 0xFFFFu);
    gdt_desc[i].gran = (uint8_t)((limit >> 16) & 0x0Fu);

    /* Available 386 TSS, present, DPL 0 */
    gdt_desc[i].access = 0x89u;
}

void tss_set_esp0(uint32_t esp)
{
    g_tss.esp0 = esp;
}

void gdt_init(void)
{
    size_t k;

    for (k = 0; k < sizeof(gdt_desc) / sizeof(gdt_desc[0]); ++k) {
        uint8_t *p = (uint8_t *)&gdt_desc[k];

        for (size_t j = 0; j < sizeof(struct gdt_entry); ++j)
            p[j] = 0;
    }

    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF);

    {
        uint8_t *tp = (uint8_t *)&g_tss;

        for (size_t j = 0; j < sizeof(g_tss); ++j)
            tp[j] = 0;

        g_tss.ss0 = 0x10u;
        g_tss.esp0 = (uint32_t)(uintptr_t)(g_null_kstack + 512);
    }

    gdt_set_tss(5, (uint32_t)(uintptr_t)&g_tss,
        (uint32_t)(sizeof(struct tss_i386) - 1u));

    {
        struct gdtr gdtr;

        gdtr.limit = (uint16_t)(sizeof(gdt_desc) - 1u);
        gdtr.base = (uint32_t)(uintptr_t)gdt_desc;

        __asm__ volatile("lgdt %0" : : "m"(gdtr));
    }

    gdt_flush();
}
