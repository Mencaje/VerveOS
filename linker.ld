ENTRY(_start)

SECTIONS
{
    . = 1M;

    kernel_image_begin = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot_header)
        *(.text .text.*)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data .data.*)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    kernel_image_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
    }
}
