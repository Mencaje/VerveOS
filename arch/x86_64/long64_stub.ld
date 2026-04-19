/* Link long64_stub.S alone; VMA does not matter (flat binary). */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(long64_entry)

SECTIONS
{
    . = 0;

    .text :
    {
        *(.text .text.*)
    }

    /DISCARD/ :
    {
        *(.note* .comment .eh_frame*)
    }
}
