# VerveOS

一个面向学习与 bring-up 的 **32 位 x86（i386）** 内核实验项目：Multiboot2 引导、物理内存位图分配器、恒等分页、**ACPI（RSDT/XSDT + MADT CPU / I/O APIC / IRQ0 覆盖）**、**LAPIC 周期定时器（向量 64）**、可选 **I/O APIC 路由**、**最小 SMP（AP 进入全局队列 + 每核 idle）**、协作式调度与内核线程演示。代码刻意保持短小可读，**不是**通用桌面或服务器操作系统。

**English (brief):** Minimal educational i386 kernel — Multiboot2, bitmap PMM, identity paging, ACPI tables (incl. XSDT), LAPIC timer for ticks, optional IOAPIC/PIC masking, experimental SMP (APs on shared runqueue), cooperative scheduling + kernel threads. Ring 0 only, no userspace/syscalls/filesystem.

### 当前里程碑（与 GitHub 同步）

**2026-04：** **x86_64（`make amd64`）** 已与主线 **bring-up 能力对齐**：Multiboot2 ELF32 外壳 → **long mode** → **`kernel_main_amd64`**（**0x200000**）；恒等分页（含 **LAPIC MMIO**）、位图 PMM、**heap**、IDT、**LAPIC 周期定时（向量 64）**、`pic_irq_mask_all`、**协作式调度 + `cpu_switch` + 线程演示**。amd64 侧仍为 **实验路径**：**真实 SMP/AP、I/O APIC** 等尚未与 i386 同级。

---

## 目录

1. [仓库里有什么](#1-仓库里有什么)
2. [构建与运行](#2-构建与运行)
3. [引导与内存布局](#3-引导与内存布局)
4. [Multiboot2](#4-multiboot2)
5. [物理内存管理 PMM](#5-物理内存管理-pmm)
6. [分页](#6-分页)
7. [内核堆 kmalloc/kfree](#7-内核堆-kmallockfree)
8. [GDT / IDT / 中断](#8-gdt--idt--中断)
9. [PIC 与 PIT](#9-pic-与-pit)
10. [串口与 VGA](#10-串口与-vga)
11. [调度与时间片钩子](#11-调度与时间片钩子)
12. [线程与上下文切换](#12-线程与上下文切换)
13. [刻意未实现的能力](#13-刻意未实现的能力)
14. [目录结构](#14-目录结构)
15. [许可证](#15-许可证)

---

## 1. 仓库里有什么

| 类别 | 说明 |
|------|------|
| **架构** | **主线 i386**：Ring 0；**最小 SMP**。另有一条 **实验性 x86_64**：`make amd64` → **ELF32 Multiboot2 外壳 + 0x200000 处 64 位内核**（分页/PMM/heap/IDT/**LAPIC 定时**/调度线程等已接；**SMP/AP、IOAPIC** 等仍弱于 i386）。 |
| **引导** | Multiboot2：i386 调 `kernel_main`；x86_64 为 **Multiboot2 的 32 位入口**（`boot_chain32`）→ 长模式桩（`long64_stub`）→ `kernel_main_amd64`（**0x200000** 处 64 位 C，**与** i386 主核不同一棵 `linker.ld`）。 |
| **ACPI** | **RSDP v2**：优先 **XSDT**，否则 **RSDT**；解析 **MADT**：CPU Local APIC、首个 **I/O APIC**、**IRQ source override（IRQ0→GSI）**。热插拔仅占位 `acpi_hotplug_stub_init`。 |
| **内存** | mmap 驱动的 **4KiB 帧** 位图分配器；内核/Multiboot 信息/位图区 marked used。 |
| **虚拟内存** | **恒等映射**；静态页目录；**MMIO 页表槽位池**（多 PD 索引可各自挂 PT，不限单张「高地址 PT」）。 |
| **堆** | BSS 内约 **256 KiB**，块头对齐；**空闲链表首次适配 + bump**。 |
| **中断 / 定时** | IDT：**异常 0–31**、**PIC IRQ 32–47**、**LAPIC 定时器 64**；定时 tick 走 **LAPIC** + `lapic_eoi`；PIC 默认可全掩；可选 **I/O APIC** 编程 legacy PIT/IRQ0 路由（演示向）。 |
| **调度** | **每 CPU** tick、`need_resched`、**每 CPU** 延后环、嵌套 **preempt_disable**；**不在 ISR 里切换线程**。 |
| **线程** | 静态池 **MAX_THREADS 64**、每核 idle（**MAX_SMP_CPU 32**）、**sched_token CAS**、环形队列、`cpu_switch`、Zombie 回收。 |
| **输出** | COM1 串口轮询输出；VGA 文本缓冲 `0xB8000` 少量提示。 |

---

## 2. 构建与运行

### 依赖

- **推荐（裸机交叉）**：`i686-elf-gcc`、`i686-elf-as`、`i686-elf-objcopy`、`i686-elf-ld`（见 OSDev Wiki [GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler)）。
- **或在 Linux/WSL 上用宿主 GCC**：`Makefile` 会在检测到 **非** `i686-elf-*` 目标时自动加 **`-m32`**，并使用系统的 `as` / `ld` / `objcopy`；链接需 **32 位 libgcc**，Ubuntu 请安装 **`gcc-multilib`**；嵌入 SMP 跳板二进制时用 **`ld -m elf_i386 -r -b binary`**，避免生成 64 位 `tramp_embed.o`。
- **GNU Make**。
- **QEMU**：运行 i386 内核用 **`qemu-system-i386`**；运行 x86_64 内核另需 **`qemu-system-x86_64`**（Ubuntu 包名 **`qemu-system-x86`**：`sudo apt install qemu-system-x86`）。

Windows 用户常用 **WSL（Ubuntu）** 或 **MSYS2**；宿主 `cc -m32` 路径需 multilib，交叉 `i686-elf-*` 路径无需。

### 命令

```bash
make                 # 默认 i386：build/verve-kernel.elf
make qemu            # qemu-system-i386 -kernel build/verve-kernel.elf -serial stdio -display none
make amd64           # x86_64 长模式：build/verve-kernel-x86_64.elf（需宿主 gcc 支持 -m64）
make qemu-amd64      # qemu-system-x86_64 -nographic -kernel …（默认 -machine pc-i440fx-3.1）
make qemu-amd64-iso  # GRUB multiboot2 ISO → QEMU -cdrom（需 grub-common、xorriso；见下）
make clean           # 清理 build 与常见 *.o
```

构建产物：

- `build/verve-kernel.elf`：32 位 i386，功能完整（本仓库原主线）。
- `build/verve-kernel-x86_64.elf`：**ELF32 i386 Multiboot2 外壳**：`boot_chain32` → `long64_stub` → **`0x200000`** 处 **ELF64** 内核（`linker_amd64_inner.ld`）。内侧已实现 **`paging_amd64`**（含 **0xFEE00000** 恒等区以访问 xAPIC）、**`pmm.c`**、**`heap.c`**、**`sched_amd64` / `thread_amd64` / `context_amd64.S`**、**`lapic_amd64`**（**LAPIC 周期定时** **vec 64** + **`lapic_eoi`**，**`pic_irq_mask_all`** 与 i386 同策略）、**`smp_stub_amd64`**（**BSP** 占位）。**`pit_amd64` 已不链入**（可再启做 PIT 实验）。**尚未**：真实 **SMP/AP**、**I/O APIC** 等与 i386 对齐。

`make amd64` 同时用到 **宿主 `cc -m32`**（外壳与 blob 嵌入链接）与 **`-m64`**（内侧 C/汇编）。**64 位** 目标文件输出在 **`build/amd64_inner/`**，与默认 `make` 在源码树生成的 **`kernel/*.o`（`-m32`）** 分离，避免混链。Ubuntu/WSL 请安装 **`gcc-multilib`**。若报错 **`code model kernel does not support PIC mode`**，Makefile 已对 **内外层** 关闭 **PIC/PIE**（`-fno-pic -fno-pie`、链接 `-no-pie`）。

校验镜像是否为 Multiboot2：**`grub-file --is-x86-multiboot2 build/verve-kernel-x86_64.elf`**（退出码为 0 表示通过）。

若 **`qemu-system-x86_64`: Error loading uncompressed kernel without PVH ELF Note**：避开默认 **`pc`**（例如 **`QEMU_AMD64_MACHINE=pc-i440fx-3.1`**）；**`-machine microvm`** 等机型可能仍要求 PVH。**`-kernel`** 在某些 QEMU 版本上长时间停在 SeaBIOS「Booting from ROM」而未进入来宾；可换 **`pc-i440fx-2.11`**、升级 QEMU，或使用 **`make qemu-amd64-iso`**（依赖 **`grub-mkrescue`**、通常 **`grub-pc-bin`** / **`xorriso`**）。ISO 若仍报 **no multiboot header**，请确认 **`grub-file`** 校验通过且 ISO 中的 **`/boot/kernel.elf`** 与 `build` 产物一致。

---

## 3. 引导与内存布局

- **`linker.ld`**：镜像从 **1 MiB** 开始；各段 **4KiB** 对齐；丢弃 `.comment`、`.eh_frame*`。
- **`arch/i386/boot.S`**：
  - 校验 Multiboot2 魔数（`EAX == 0x36d76289`）。
  - 内核栈 **约 32 KiB**（`stack_bottom`～`stack_top`）。
  - 调用 `kernel_main(magic, mb_info_phys)`（`EBX` 指向 Multiboot2 信息区物理地址）。
- **无** C 运行时初始化；编译选项 `-ffreestanding -nostdlib`。

---

## 4. Multiboot2

实现文件：`kernel/mboot2.c`、`include/verve/mboot2.h`。

### 已实现

- 遍历 tag 链表，识别：
  - **`MB2_TAG_MMAP`**：按 entry 遍历，支持对 **可用区（type=1）** 回调。
  - **`MB2_TAG_BASIC_MEMINFO`**：`mem_lower_kb` / `mem_upper_kb`（主要用于串口日志；PMM 以 mmap 为准）。
- 提供 API：
  - `mb2_sum_usable_ram` — 统计可用物理内存总量。
  - `mb2_read_basic_meminfo` — 读传统 meminfo。
  - `mb2_for_each_available` — PMM 用于找最大物理末端、释放可用帧。

### 未使用/未深入

- 命令行、引导器名称、ELF 符号、framebuffer 等 tag **未专门使用**。
- **ACPI New/Old** 等 tag 由 **`mb2_find_rsdp_payload` + `acpi_hw_init`** 走 RSDP 物理区；**不**再要求 Multiboot2 必须带其它 ACPI 专属 tag。

---

## 5. 物理内存管理（PMM）

实现：`kernel/pmm.c`。

### 策略

- **帧大小**：4096 字节（`FRAME_SHIFT == 12`）。
- **位图**：置于 **`kernel_image_end` 向上对齐** 的第一个物理页起；位图所占页也 **标为已用**。

### 启动时标为「已占用」的物理区

1. 整个内核映像：`kernel_image_begin`～`kernel_image_end`。
2. Multiboot2 信息结构：`mb_info_phys`～`+ total_size`。
3. 位图本体区域。
4. **物理帧 0** 永久保留（降低空指针误当时被分配的概率）。

### API

- `pmm_init`、`pmm_alloc_frame`、`pmm_free_frame`、`pmm_frame_count`、`pmm_max_pfn`。

### 特性与局限

- `pmm_alloc_frame`：从 PFN 1 起 **线性扫描**，**O(n)**。
- **无** ZONE、**无** NUMA、**无** 伙伴系统合并（那是上层策略）；当前仅有位图。
- **当前内核堆未使用 PMM 扩展**：`kmalloc` 使用静态 BSS  arena；`main.c` 里仅演示性地分配/释放一帧。

---

## 6. 分页

实现：`arch/i386/paging.c`。

### 已实现

- `paging_identity_init(max_pfn)`：
  - 构建 **恒等映射**：线性地址 **等于** 物理地址。
  - 静态 `boot_pd[1024]` + **`boot_pt[MAX_ID_PT][1024]`**，其中 **`MAX_ID_PT == 128`**。
  - 页表项：**Present + Read/Write**（内核可写）。
  - 加载 CR3，并在 CR0 未置 PG 时打开分页。
- **MMIO / 稀疏映射**：多次 `mmio_pt_one` / `paging_map_identity_page` 时，使用 **多张页表层级槽位**（`MMIO_PT_SLOTS`），避免「假设只有一个高 half PD」导致冲突。

### 局限

- **无** 用户/管理员页（U/S）、**无** NX、**无** PAE；未使用大型页（2M/4M）。
- 若机器物理内存跨度需要 **超过 128 张页表**，`paging_identity_init` 会失败并打印错误（需改大 `MAX_ID_PT` 并增加静态表占用）。

---

## 7. 内核堆 kmalloc/kfree

实现：`kernel/heap.c`。

- **容量**：约 **256 KiB** 连续 BSS，**16 字节**对齐块。
- **块布局**：首部 `heap_hdr.total`（整个块总长），用户指针在 **`HEAP_HDR_USER_OFFSET`** 之后。
- **分配**：优先在 **首次适配** 空闲链表上取块；否则 **bump** 向上增长。
- **释放**：`kfree` 校验指针落在堆区内后链入空闲表；非法指针 **静默忽略**。
- **未实现**：与 PMM 联动扩大堆、伙伴合并、Slab、毒化与 use-after-free 检测等。

---

## 8. GDT / IDT / 中断

### GDT（`arch/i386/gdt.c`）

- 平坦模型：**代码** 与 **数据** 段覆盖 **0～4G**，Ring 0。
- **无** Ring 3 描述符、**无** TSS（任务切换不靠硬件任务门）。

### IDT（`arch/i386/idt.c`）

- **向量 0～31**：CPU 异常，接入 `exc_asm.S` 桩。
- **向量 32～47**：PIC IRQ，接入 `irq_asm.S` 桩。
- **向量 64**：LAPIC 定时器，`irq_asm.S` 桩 → `sched_timer_tick()` + `lapic_eoi()`。
- 门属性固定为项目中使用的 **32 位 interrupt gate** 配置（`0x8E`）。

### 汇编桩

- **`exc_asm.S`**：区分带/不带 error code；`pusha` → 压栈 `verve_exc_frame*` → `verve_exc_dispatch`。
- **`irq_asm.S`**：IRQ 压 vector；同样进入 `verve_irq_dispatch`。

### C 层分发（`kernel/interrupt.c`）

- **`verve_irq_dispatch`**：**`vec == 64`**：LAPIC 定时器，`sched_timer_tick()` + **`lapic_eoi()`**。**`32 ≤ vec < 48`**：PIC/IOAPIC 向量；若仍用 PIC 8259，`pic_send_eoi`；若启用 LAPIC EOI（定时已迁 LAPIC），对该路径可走 `lapic_eoi`。误用向量会打印并 **halt**。
- **`verve_exc_dispatch`**：串口打印异常号、错误码、EIP/CS；**#14 页故障**额外打印 **CR2**；然后 **永久 halt** —— **不支持** 从异常返回继续执行。

---

## 9. PIC、PIT、I/O APIC 与 LAPIC（当前主线）

### 8259 PIC（`arch/i386/pic.c`）

- 初始化级联；**Master 向量基 0x20**、**Slave 0x28**（与 IDT 32–47 对齐）。
- **`pic_irq_mask_all`**：**全部 IRQ 线屏蔽**，避免与 LAPIC-only 定时路径重复抢中断（若仅用 LAPIC tick，可不依赖 PIC IRQ0）。
- **旧路径**（若仍演示 PIT）：曾 **`pic_irq_unmask(0)`** 打开 IRQ0；当前 **`kernel_main`** 优先 **ACPI + LAPIC 定时**，PIT 非调度主时钟。

### PIT 8254（`arch/i386/pit.c`）

- `pit_set_frequency_hz` **仍可用**；默认启动序列以 **LAPIC 周期定时器** 为主调度时钟（约 50 Hz 量级，依 `lapic_timer_periodic` 参数与硬件而定）。

### I/O APIC（`arch/i386/ioapic.c`）

- 若 MADT 提供 **I/O APIC** 与 **IRQ0→GSI**，可 **MMIO 映射** 并写 RTE，将 legacy 定时器线接到 **向量 32**、**BSP** 等（与教材式演示一致）；与 **全掩 PIC** 可同时存在。

### Local APIC（`arch/i386/lapic.c`）

- BSP/AP：**MMIO**（默认 `0xFEE00000`）+ **`lapic_bsp_early_init`** / AP 在线使能。
- **`lapic_timer_periodic`**：**向量 64** 周期模式 + **EOI**；与 `irq_timer_set_lapic_eoi` 配合。

---

## 10. 串口与 VGA

- **串口**：`kernel/serial.c`，**COM1 `0x3F8`**，轮询发送；**未**编程波特率（依赖固件/QEMU 默认）。
- **VGA**：`kernel/main.c` 直接写 **`0xB8000`** 彩色文本模式；清屏与少量状态行；**无** 通用控制台抽象。

---

## 11. 调度与时间片钩子

实现：`kernel/sched.c`。

### 全局状态（每 CPU）

- **`g_tick[cpu]`**：该 CPU 上 LAPIC（或遗留 IRQ0）每次 tick 递增。
- **`g_need_resched[cpu]`**：每 `PREEMPT_INTERVAL_TICKS`（**5**）个 tick 置位一次。
- **延后环形队列**：**每 CPU** 一份（容量 16），满时溢出计数。
- **`g_preempt_disable_depth`**：嵌套临界区；大于 0 时 **`schedule`/`sched_yield`** 路径应直接返回（见 `sched_preempt_disabled`）。

### 重要设计约束

- **`cpu_switch` 不在 IRQ 里执行**：定时器 **只设置标志与延后记录**；线程在 **`sched_checkpoint()`**（或主动 **`sched_yield()`**）里再处理抢占意愿。因此这是 **IRQ 驱动的「抢占意愿」+ 线程内协作切换点**，**不是** 典型的「时钟中断内直接换栈」的全抢占内核。

### 调试输出

- 前若干次 tick 会串口打印；环形队列排空时也会打印 `drained` 等（便于确认 **deferred** 路径在 **线程上下文** 执行）。

---

## 12. 线程与上下文切换

实现：`kernel/thread.c`、`arch/i386/context.S`。

### 数据结构与限制

- 静态数组 **`threads[MAX_THREADS]`**，`MAX_THREADS == 64`。
- **`main_thread`**：作为环形队列头，**`kstack == NULL`**（使用引导栈）。
- 子线程：栈由 **`kmalloc(STACK_SIZE)`**，`STACK_SIZE == 4096`。
- **SMP**：**`g_current[MAX_SMP_CPU]`**，**每核 idle**（`affinity_cpu == 逻辑 CPU 号`），**`sched_token` CAS** 认领可运行线程，避免多核同时拉同一任务。

### 调度

- **全局环形链表** 运行队列；**`pick_next`** 只选择 **`THREAD_RUNNABLE`** 且 **affinity 匹配**的线程。
- **`sched_yield`**：保存/恢复标志位；持自旋锁选下一个线程；**`cpu_switch(&old->esp, next->esp)`**。
- **`cpu_switch`**：保存 callee-saved（`ebp, ebx, esi, edi`）与返回点，切换 `esp`；**不保存 FPU/SSE**，**不换 CR3**。

### 生命周期

1. **`thread_spawn(fn)`**：分配槽位与栈，初始栈顶布置为 **`thread_trampoline`**。
2. Trampoline 调 `fn()`；返回后将状态置 **`THREAD_ZOMBIE`**，然后死循环 **`sched_yield()`**。
3. 在 **从该线程切换出去之后**，`thread_reap_yield_from` 会对 **ZOMBIE** 线程：**从队列摘除**、**`kfree` 内核栈**、槽位 **`THREAD_UNUSED`**。

### 同步

- 运行队列与部分状态由 **`verve_spinlock`**（xchg + pause）保护。

### 演示

- **`thread_demo_run()`**（由 `kernel_main` 调用）：spawn 两个 worker，与 main 一起做 **round-robin + timer checkpoint** 演示。

---

## 13. 仍刻意未实现或仅限「够用」的能力

以下仍 **不存在**、**极简化**，或仅为 **占位/可扩展**：

| 领域 | 说明 |
|------|------|
| 用户态 | 无 Ring 3、无 **syscall/sysenter**、无用户进程/ELF 加载。 |
| 完整虚拟内存 | 无进程地址空间、无 COW、无按需分页、页错误不可恢复。 |
| 真正「硬」抢占 | 定时器只置位 **需要重调度**；**不在 ISR 里换栈**，需 **checkpoint/yield**。 |
| 睡眠/阻塞 | 无等待队列；无「可阻塞的 mutex」。 |
| 文件系统 / 块设备 | 无。 |
| 网络 | 无。 |
| 键盘/磁盘等 IRQ | 除已用路径外，多数设备 IRQ 未做驱动示例。 |
| **SMP 完整性** | 无 IPI 迁移、无 per-CPU 队列隔离、无 TSS/IST；**最小** AP 上线 + 共享 runqueue + per-CPU 定时 tick。 |
| **ACPI** | **无复杂热插拔运行时**（仅 `acpi_hotplug_stub_init`）；RSDP **仅支持表指针落在低 4G 路径**（与当前恒等映射策略一致）。 |
| 电源管理 | 无休眠/关机策略。 |

---

## 14. 目录结构（概览）

```
VerveOS/
├── Makefile
├── linker.ld
├── linker_amd64.ld           # x86_64 链接脚本
├── README.md                 # 本文件
├── arch/i386/
│   ├── boot.S                # Multiboot2、_start、内核栈
│   ├── context.S             # cpu_switch
│   ├── exc_asm.S / irq_asm.S # 异常/IRQ 桩（含 vec 64）
│   ├── gdt.c / gdt_asm.S
│   ├── idt.c
│   ├── paging.c
│   ├── pic.c / pit.c
│   ├── lapic.c               # Local APIC、定时
│   ├── ioapic.c              # I/O APIC RTE（可选）
│   ├── smp_tramp.S / smp_tramp.ld   # AP 跳板
├── arch/x86_64/
│   ├── boot_chain.S          # 32 位入核 → 长模式 → kernel_main_amd64
├── include/
│   ├── arch/i386/            # io、pic、pit、lapic、ioapic
│   ├── arch/x86_64/          # io（inb/outb，与 32 位同语意）
│   └── verve/                # acpi_hw、heap、interrupt、mboot2、paging、pmm、sched、smp、smp_boot、serial、spinlock、thread …
└── kernel/
    ├── main.c                # ACPI / GDT / IDT / LAPIC / 线程 / SMP / 定时 / 演示
    ├── main_amd64.c          # x86_64 最小 C 入口（与 i386 主线分离）
    ├── acpi_hw.c / acpi_smp.c
    ├── mboot2.c / pmm.c / heap.c
    ├── interrupt.c / sched.c / serial.c / thread.c / smp.c
```

---

## 15. 许可证

若本仓库根目录包含 `LICENSE` 文件，以该文件为准；**若尚未添加**，在补充许可证之前，默认 **All Rights Reserved**（或按组织策略在 GitHub 上选择模板）。建议在仓库设置中明确 **开源许可证** 以便他人合法使用与再分发。

---

## 致谢与参考

- [OSDev Wiki](https://wiki.osdev.org/) — Multiboot、PMM、IDT、PIC 等大量资料。
- Multiboot2 规范 — 引导与信息区格式。

---

*文档与当前源码树一致；若实现演进，请同步更新本 README。*
