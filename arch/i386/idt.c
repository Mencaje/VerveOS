#include <verve/interrupt.h>

#include <stddef.h>
#include <stdint.h>

extern void isr_0(void);
extern void isr_1(void);
extern void isr_2(void);
extern void isr_3(void);
extern void isr_4(void);
extern void isr_5(void);
extern void isr_6(void);
extern void isr_7(void);
extern void isr_8(void);
extern void isr_9(void);
extern void isr_10(void);
extern void isr_11(void);
extern void isr_12(void);
extern void isr_13(void);
extern void isr_14(void);
extern void isr_15(void);
extern void isr_16(void);
extern void isr_17(void);
extern void isr_18(void);
extern void isr_19(void);
extern void isr_20(void);
extern void isr_21(void);
extern void isr_22(void);
extern void isr_23(void);
extern void isr_24(void);
extern void isr_25(void);
extern void isr_26(void);
extern void isr_27(void);
extern void isr_28(void);
extern void isr_29(void);
extern void isr_30(void);
extern void isr_31(void);

extern void irq_32(void);
extern void irq_33(void);
extern void irq_34(void);
extern void irq_35(void);
extern void irq_36(void);
extern void irq_37(void);
extern void irq_38(void);
extern void irq_39(void);
extern void irq_40(void);
extern void irq_41(void);
extern void irq_42(void);
extern void irq_43(void);
extern void irq_44(void);
extern void irq_45(void);
extern void irq_46(void);
extern void irq_47(void);

extern void irq_64(void);

struct idt_gate {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t ist_zero;
    uint8_t flags;
    uint16_t off_hi;
} __attribute__((packed));

static struct idt_gate idt[256];

struct idtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static void idt_set(uint8_t vec, uintptr_t handler)
{
    uint32_t h = (uint32_t)handler;

    idt[vec].off_lo = (uint16_t)(h & 0xFFFFu);
    idt[vec].sel = 0x08;
    idt[vec].ist_zero = 0;
    idt[vec].flags = 0x8Eu;
    idt[vec].off_hi = (uint16_t)((h >> 16) & 0xFFFFu);
}

void idt_init(void)
{
    typedef void (*isr_fn)(void);

    static const isr_fn stubs[32] = {
        isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
        isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
        isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
        isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    };

    for (size_t i = 0; i < 256; ++i) {
        idt[i].off_lo = 0;
        idt[i].sel = 0;
        idt[i].ist_zero = 0;
        idt[i].flags = 0;
        idt[i].off_hi = 0;
    }

    for (size_t i = 0; i < 32; ++i)
        idt_set((uint8_t)i, (uintptr_t)stubs[i]);

    {
        typedef void (*irq_fn)(void);

        static const irq_fn irq_stubs[16] = {
            irq_32, irq_33, irq_34, irq_35, irq_36, irq_37, irq_38, irq_39,
            irq_40, irq_41, irq_42, irq_43, irq_44, irq_45, irq_46, irq_47,
        };

        for (size_t i = 0; i < 16; ++i)
            idt_set((uint8_t)(32 + i), (uintptr_t)irq_stubs[i]);
    }

    idt_set(64u, (uintptr_t)irq_64);

    struct idtr idtr;

    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.base = (uint32_t)(uintptr_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idtr));
}
