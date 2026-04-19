OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(smp_trampoline_start)

SECTIONS
{
    . = 0x8000;

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
