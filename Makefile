# VerveOS — Multiboot2 kernel
#
# i386 (default):   make
# x86_64 long mode: make amd64   (same Multiboot2 entry → switch to long mode)

ARCH ?= i386

BASE_CFLAGS := -std=c11 -ffreestanding -O2 -Wall -Wextra -nostdlib \
	-fno-builtin -fno-stack-protector -I include

LDFLAGS_STATIC := -nostdlib -static

CC ?= i686-elf-gcc

ifeq ($(ARCH),x86_64)

AS       ?= as
LD       ?= ld
OBJCOPY  ?= objcopy

# Inner ELF64（kernel_main_amd64）：独立链接后再打成扁平 blob，嵌入 ELF32 multiboot 镜像。
CFLAGS_INNER := $(BASE_CFLAGS) -m64 -mcmodel=kernel -mno-red-zone -fno-pic -fno-pie
LDFLAGS_INNER := $(LDFLAGS_STATIC) -m64 -no-pie -Wl,--build-id=none

# GRUB/QEMU 的 Multiboot2 探测面向 ELF32 i386；外层镜像仅含 boot32 + blob。
CFLAGS_BOOT32 := $(BASE_CFLAGS) -m32 -fno-pic -fno-pie
LDFLAGS_BOOT := $(LDFLAGS_STATIC) -m32 -no-pie -Wl,--build-id=none

CC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)

OUT := build/verve-kernel-x86_64.elf

# 与宿主 i386 的 kernel/*.o 分离，避免混用 -m32 / -m64 产物
AMD64_OBJDIR := build/amd64_inner

AMD64_INNER_OBJS := $(AMD64_OBJDIR)/arch/x86_64/paging_amd64.o \
	$(AMD64_OBJDIR)/arch/x86_64/exc_amd64.o $(AMD64_OBJDIR)/arch/x86_64/irq_amd64.o \
	$(AMD64_OBJDIR)/arch/x86_64/idt_amd64.o $(AMD64_OBJDIR)/arch/x86_64/pic_amd64.o \
	$(AMD64_OBJDIR)/arch/x86_64/lapic_amd64.o $(AMD64_OBJDIR)/arch/x86_64/context_amd64.o \
	$(AMD64_OBJDIR)/kernel/interrupt_amd64.o \
	$(AMD64_OBJDIR)/kernel/main_amd64.o $(AMD64_OBJDIR)/kernel/mboot2.o \
	$(AMD64_OBJDIR)/kernel/serial.o $(AMD64_OBJDIR)/kernel/pmm.o \
	$(AMD64_OBJDIR)/kernel/heap.o $(AMD64_OBJDIR)/kernel/sched_amd64.o \
	$(AMD64_OBJDIR)/kernel/thread_amd64.o $(AMD64_OBJDIR)/kernel/smp_stub_amd64.o

AMD64_LONG64_STUB_O := $(AMD64_OBJDIR)/arch/x86_64/long64_stub.o
AMD64_INNER_ELF := build/verve-inner.elf
AMD64_INNER_BIN := build/verve-inner.bin
AMD64_INNER_BLOB := build/inner_elf32_blob.o
AMD64_LONG64_ELF := build/long64_stub.elf
AMD64_LONG64_BIN := build/long64_stub.bin
AMD64_LONG64_BLOB := build/long64_elf32_blob.o

LIBGCC_INNER := $(shell $(CC) $(CFLAGS_INNER) -print-libgcc-file-name 2>/dev/null)

else

LDFLAGS := $(LDFLAGS_STATIC)

CC_MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ifeq ($(findstring i686-elf,$(CC_MACHINE)),)
AS      ?= as
LD      ?= ld
OBJCOPY ?= objcopy
CFLAGS := $(BASE_CFLAGS) -m32
LDFLAGS += -m32
else
AS      ?= i686-elf-as
LD      ?= i686-elf-ld
OBJCOPY ?= i686-elf-objcopy
CFLAGS := $(BASE_CFLAGS)
endif

ARCH_DIR := arch/i386

ARCH_ASM := $(ARCH_DIR)/boot.S $(ARCH_DIR)/exc_asm.S $(ARCH_DIR)/gdt_asm.S \
	$(ARCH_DIR)/irq_asm.S $(ARCH_DIR)/context.S $(ARCH_DIR)/user_asm.S

ARCH_C := $(ARCH_DIR)/paging.c $(ARCH_DIR)/gdt.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/pic.c $(ARCH_DIR)/pit.c $(ARCH_DIR)/lapic.c $(ARCH_DIR)/ioapic.c

KERN_SRC := kernel/main.c kernel/mboot2.c kernel/serial.c kernel/pmm.c kernel/heap.c \
	kernel/interrupt.c kernel/syscall.c kernel/proc.c kernel/user_bringup.c kernel/sched.c kernel/thread.c \
	kernel/vfs_ram.c kernel/exec_flat.c \
	kernel/acpi_hw.c kernel/acpi_smp.c kernel/smp.c

OBJS := $(ARCH_ASM:.S=.o) $(KERN_SRC:.c=.o) $(ARCH_C:.c=.o) build/tramp_embed.o

OUT := build/verve-kernel.elf
LINKER := linker.ld

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name 2>/dev/null)

endif

.PHONY: all amd64 clean qemu qemu-amd64 qemu-amd64-iso

all: $(OUT)

amd64:
	@$(MAKE) ARCH=x86_64 CC=cc build/verve-kernel-x86_64.elf

ifeq ($(ARCH),x86_64)

$(OUT): linker_amd64_boot.ld arch/x86_64/boot_chain32.o $(AMD64_LONG64_BLOB) $(AMD64_INNER_BLOB)
	mkdir -p build
	$(CC) $(CFLAGS_BOOT32) $(LDFLAGS_BOOT) -Wl,-T,linker_amd64_boot.ld -o $@ \
		arch/x86_64/boot_chain32.o $(AMD64_LONG64_BLOB) $(AMD64_INNER_BLOB)

$(AMD64_INNER_ELF): linker_amd64_inner.ld $(AMD64_INNER_OBJS)
	mkdir -p build
	$(CC) $(CFLAGS_INNER) $(LDFLAGS_INNER) -Wl,-T,linker_amd64_inner.ld -o $@ \
		$(AMD64_INNER_OBJS) $(LIBGCC_INNER)

$(AMD64_INNER_BIN): $(AMD64_INNER_ELF)
	$(OBJCOPY) -O binary $< $@

$(AMD64_INNER_BLOB): $(AMD64_INNER_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@.tmp
	$(OBJCOPY) --rename-section .data=.kernel_inner $@.tmp $@
	rm -f $@.tmp

$(AMD64_LONG64_STUB_O): arch/x86_64/long64_stub.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_INNER) -Wa,-I$(CURDIR)/include -c -o $@ $<

$(AMD64_LONG64_ELF): arch/x86_64/long64_stub.ld $(AMD64_LONG64_STUB_O)
	mkdir -p build
	$(LD) -m elf_x86_64 -T arch/x86_64/long64_stub.ld -nostdlib -o $@ $(AMD64_LONG64_STUB_O)

$(AMD64_LONG64_BIN): $(AMD64_LONG64_ELF)
	$(OBJCOPY) -O binary $< $@

$(AMD64_LONG64_BLOB): $(AMD64_LONG64_BIN)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@.tmp
	$(OBJCOPY) --rename-section .data=.long64_blob $@.tmp $@
	rm -f $@.tmp

arch/x86_64/boot_chain32.o: arch/x86_64/boot_chain32.S
	$(CC) $(CFLAGS_BOOT32) -c -o $@ $<

$(AMD64_OBJDIR)/arch/x86_64/%.o: arch/x86_64/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_INNER) -c -o $@ $<

$(AMD64_OBJDIR)/arch/x86_64/%.o: arch/x86_64/%.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_INNER) -c -o $@ $<

$(AMD64_OBJDIR)/kernel/%.o: kernel/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS_INNER) -c -o $@ $<

else

$(OUT): $(LINKER) $(OBJS)
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,$(LINKER) -o $@ $(OBJS) $(LIBGCC)

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

endif

SMP_QEMU ?= 2

qemu: build/verve-kernel.elf
	qemu-system-i386 -kernel build/verve-kernel.elf -serial stdio -display none -smp $(SMP_QEMU)

# QEMU 6+ 默认 pc 机常要求 PVH ELF Note；Multiboot2 裸 ELF 用旧 i440fx 机型可从 -kernel 正常加载。
QEMU_AMD64_MACHINE ?= pc-i440fx-3.1

qemu-amd64:
	@$(MAKE) ARCH=x86_64 CC=cc build/verve-kernel-x86_64.elf
	qemu-system-x86_64 -machine type=$(QEMU_AMD64_MACHINE) -nographic -kernel build/verve-kernel-x86_64.elf

# GRUB 通过 multiboot2 加载 ELF64，比部分 QEMU -kernel 路径更稳（需 grub-common / xorriso）
AMD64_ISO_DIR := build/grub_amd64_iso

qemu-amd64-iso:
	@$(MAKE) ARCH=x86_64 CC=cc build/verve-kernel-x86_64.elf
	mkdir -p $(AMD64_ISO_DIR)/boot/grub
	cp build/verve-kernel-x86_64.elf $(AMD64_ISO_DIR)/boot/kernel.elf
	printf '%s\n' \
		'search --set=root --file /boot/kernel.elf' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "VerveOS amd64" {' \
		'  multiboot2 /boot/kernel.elf' \
		'  boot' \
		'}' > $(AMD64_ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o build/verve-amd64.iso $(AMD64_ISO_DIR)
	qemu-system-x86_64 -cdrom build/verve-amd64.iso -nographic

clean:
	rm -rf build
	rm -rf build/amd64_inner
	rm -f arch/x86_64/*.o arch/i386/*.o kernel/*.o $(OUT) build/verve-kernel.elf build/verve-kernel-x86_64.elf
