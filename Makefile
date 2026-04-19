# VerveOS — minimal bootable kernel (Multiboot2, i386)

# Requires a freestanding cross toolchain, e.g. i686-elf-gcc / i686-elf-ld

# MSYS2: pacman -S mingw-w64-i686-gcc (then use i686-w64-mingw32-gcc won't work for -ffreestanding;

#       install a true i686-elf toolchain or build from https://wiki.osdev.org/GCC_Cross-Compiler )

#

# Typical: CC=i686-elf-gcc  LD=i686-elf-ld  AS=i686-elf-as  OBJCOPY=i686-elf-objcopy



AS      ?= i686-elf-as

CC      ?= i686-elf-gcc

OBJCOPY ?= i686-elf-objcopy



CFLAGS  := -std=c11 -ffreestanding -O2 -Wall -Wextra -nostdlib \

           -fno-builtin -fno-stack-protector -I include

LDFLAGS := -nostdlib -static



ARCH_DIR := arch/i386

ARCH_ASM := $(ARCH_DIR)/boot.S $(ARCH_DIR)/exc_asm.S $(ARCH_DIR)/gdt_asm.S \

            $(ARCH_DIR)/irq_asm.S $(ARCH_DIR)/context.S

ARCH_C   := $(ARCH_DIR)/paging.c $(ARCH_DIR)/gdt.c $(ARCH_DIR)/idt.c \

            $(ARCH_DIR)/pic.c $(ARCH_DIR)/pit.c

KERN_SRC := kernel/main.c kernel/mboot2.c kernel/serial.c kernel/pmm.c \

            kernel/heap.c kernel/interrupt.c kernel/sched.c kernel/thread.c



OBJS := $(ARCH_ASM:.S=.o) $(KERN_SRC:.c=.o) $(ARCH_C:.c=.o)



.PHONY: all clean qemu



all: build/verve-kernel.elf



$(ARCH_DIR)/%.o: $(ARCH_DIR)/%.S

	$(AS) -32 -o $@ $<



kernel/%.o: kernel/%.c

	$(CC) $(CFLAGS) -c -o $@ $<



$(ARCH_DIR)/%.o: $(ARCH_DIR)/%.c

	$(CC) $(CFLAGS) -c -o $@ $<



build/verve-kernel.elf: linker.ld $(OBJS)

	mkdir -p build

	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,linker.ld -o $@ $(OBJS) -lgcc



qemu: build/verve-kernel.elf

	qemu-system-i386 -kernel build/verve-kernel.elf -serial stdio -display none



clean:

	rm -rf build $(OBJS)


