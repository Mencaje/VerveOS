OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

/*
 * Physical addresses also in include/arch/x86_64/boot_layout.h — keep in sync.
 * VERV_SAVED_MAGIC_PHYS / VERV_INNER_PHYS
 */

VERV_SAVED_MAGIC_PHYS = 0x0010C000;
VERV_INNER_PHYS       = 0x00200000;

SECTIONS
{
    . = 0x100000;

    /* 与 .text 同段、同 R E；Multiboot2 头后对齐到页，入口落在 0x101000。 */
    .text ALIGN(8) :
    {
        KEEP(*(.multiboot_header))
        . = ALIGN(4096);
        *(.text .text.*)
    }

    .rodata ALIGN(4096) :
    {
        *(.rodata .rodata.*)
    }

    /* Multiboot2 magic + EBX saved here (fixed VA for long64_stub). */
    . = VERV_SAVED_MAGIC_PHYS;
    .saved_mb2 ALIGN(16) :
    {
        *(.saved_mb2 .saved_mb2.*)
    }

    .data ALIGN(4096) :
    {
        *(.data .data.*)
    }

    .bss :
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    /* 64-bit trampoline; boot32 ljmpl lands at long64_entry. */
    . = ALIGN(4096);
    PROVIDE(long64_entry = .);
    .long64_blob :
    {
        KEEP(*(.long64_blob))
    }

    . = VERV_INNER_PHYS;
    PROVIDE(kernel_inner_begin = .);
    .kernel_inner :
    {
        KEEP(*(.kernel_inner))
    }
    PROVIDE(kernel_inner_end = .);
}
