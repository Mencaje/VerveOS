OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main_amd64)

/*
 * Long-mode C kernel (Multiboot2 info from boot32). Loaded as a flat blob at this
 * physical address by the ELF32 multiboot image.
 */
SECTIONS
{
    . = 0x200000;

    kernel_inner_begin = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
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

    kernel_inner_end = .;

    kernel_image_begin = kernel_inner_begin;
    kernel_image_end = kernel_inner_end;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
    }
}
