/*
 * Physical layout shared by boot32 linker script, boot_chain32.S and long64_stub.S.
 * Identity-mapped region; must stay in sync manually.
 */
#ifndef ARCH_X86_64_BOOT_LAYOUT_H
#define ARCH_X86_64_BOOT_LAYOUT_H

#define VERV_SAVED_MAGIC_PHYS   0x0010C000u
#define VERV_MB_INFO_PHYS       0x0010C004u
#define VERV_STACK64_TOP_PHYS   0x00120000u
#define VERV_INNER_PHYS         0x00200000u

#endif
