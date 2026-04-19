# VerveOS — minimal bootable kernel (Multiboot2, i386)
#
# Requires: i686-elf-gcc / i686-elf-ld (see README or OSDev cross-compiler wiki)
#
# Typical: CC=i686-elf-gcc LD=i686-elf-ld AS=i686-elf-as OBJCOPY=i686-elf-objcopy

CC      ?= i686-elf-gcc

CFLAGS := -std=c11 -ffreestanding -O2 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector -I include

LDFLAGS := -nostdlib -static

# Match binutils to compiler: host gcc/clang uses as/ld/objcopy; i686-elf-gcc uses i686-elf-*.
CC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ifeq ($(findstring i686-elf,$(CC_MACHINE)),)
AS      ?= as
LD      ?= ld
OBJCOPY ?= objcopy
CFLAGS += -m32
LDFLAGS += -m32
else
AS      ?= i686-elf-as
LD      ?= i686-elf-ld
OBJCOPY ?= i686-elf-objcopy
endif

# Exact libgcc.a for current $(CC)+$(CFLAGS). Host + -m32 needs 32-bit libgcc (Ubuntu: gcc-multilib).
LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name 2>/dev/null)

ARCH_DIR := arch/i386

ARCH_ASM := $(ARCH_DIR)/boot.S $(ARCH_DIR)/exc_asm.S $(ARCH_DIR)/gdt_asm.S $(ARCH_DIR)/irq_asm.S $(ARCH_DIR)/context.S

ARCH_C := $(ARCH_DIR)/paging.c $(ARCH_DIR)/gdt.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/pic.c $(ARCH_DIR)/pit.c $(ARCH_DIR)/lapic.c $(ARCH_DIR)/ioapic.c

KERN_SRC := kernel/main.c kernel/mboot2.c kernel/serial.c kernel/pmm.c kernel/heap.c kernel/interrupt.c kernel/sched.c kernel/thread.c kernel/acpi_hw.c kernel/acpi_smp.c kernel/smp.c

OBJS := $(ARCH_ASM:.S=.o) $(KERN_SRC:.c=.o) $(ARCH_C:.c=.o) build/tramp_embed.o

.PHONY: all clean qemu

all: build/verve-kernel.elf

$(ARCH_DIR)/%.o: $(ARCH_DIR)/%.S
	$(AS) -32 -o $@ $<

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(ARCH_DIR)/%.o: $(ARCH_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

build/smp_tramp.o: arch/i386/smp_tramp.S
	mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

build/smp_tramp.elf: build/smp_tramp.o arch/i386/smp_tramp.ld
	mkdir -p build
	$(LD) -nostdlib -m elf_i386 -T arch/i386/smp_tramp.ld -o $@ $<

build/smp_tramp.bin: build/smp_tramp.elf
	mkdir -p build
	$(OBJCOPY) -O binary $< $@

build/tramp_embed.o: build/smp_tramp.bin
	mkdir -p build
	$(LD) -m elf_i386 -r -b binary $< -o $@

build/verve-kernel.elf: linker.ld $(OBJS)
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,linker.ld -o $@ $(OBJS) $(LIBGCC)

SMP_QEMU ?= 2

qemu: build/verve-kernel.elf
	qemu-system-i386 -kernel build/verve-kernel.elf -serial stdio -display none -smp $(SMP_QEMU)

clean:
	rm -rf build $(OBJS)
