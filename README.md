# VerveOS

一个面向学习与 bring-up 的 **32 位 x86（i386）** 内核实验项目：Multiboot2 引导、物理内存位图分配器、恒等分页、**ACPI（RSDT/XSDT + MADT CPU / I/O APIC / IRQ0 覆盖）**、**LAPIC 周期定时器（向量 64）**、可选 **I/O APIC 路由**、**最小 SMP（AP 进入全局队列 + 每核 idle）**、协作式调度与内核线程演示；在 **Ring 3** 上已实现 **`int 0x80`** 系统调用分发、**每进程页目录（CR3）**、**用户指针校验**、**内置 ramfs**（只读编译期文件、**OPEN/READ/CLOSE**）、**`SYS_BRK`**（页对齐扩展堆）、**平面镜像装入**（**`/exit.bin` → `exec_flat`**）与 **`user_bringup`** 机器码冒烟（**读 `/hello.txt` 写串口**）。**尚无** fork、ELF 装载器、多进程调度、libc。代码刻意保持短小可读，**不是**通用桌面或服务器操作系统。

**English (brief):** Minimal educational i386 kernel — Multiboot2, bitmap PMM, identity paging, ACPI (XSDT/MADT, IOAPIC optional), LAPIC timer, SMP bring-up with shared runqueue + per-CPU idle, cooperative scheduling and kernel threads. **Ring 3:** `int 0x80` syscalls (**WRITE / GETPID / EXIT / OPEN / READ / CLOSE / BRK**), per-process PD + CR3 switch, built-in ramfs, flat binary loader (`exec_flat`), and scripted user smoke (`user_bringup`). No fork, ELF loader, or full libc.

### 当前里程碑（与 GitHub 同步）

**2026-04：** **i386** 主线含 **用户态_syscall + ramfs + brk + exec_flat**；**x86_64（`make amd64`）** 已与部分 bring-up 能力对齐（Multiboot2 ELF32 外壳 → long mode → **`kernel_main_amd64`**），但 **amd64 尚未包含上述 Ring 3 用户态全套路径**（仍以 i386 为准）。amd64 侧 **真实 SMP/AP、I/O APIC** 等弱于 i386。


<h3 id="hw-compat-i386">硬件与兼容范围（i386 主线）</h3>

**目标**是在 **典型 PC/AT 兼容机**（含多数 **英特尔 32 位 x86**）上，通过 **Multiboot2（如 GRUB）** 引导并尽量 **一条 i386 内核镜像**跑通——按 **指令集 / 芯片组能力**演进，**不**按 CPU 商品型号逐一「绑定」。

**工程上不作**「凡是英特尔 32 位 CPU 一律保证可运行」的承诺：固件、ACPI、芯片组、串口地址、内存布局等差异会导致个别机器需要单独踩坑；本项目以 **教学与 bring-up** 为主，兼容范围随 **实机与 QEMU 验证**逐步写清。

**English:** We aim at broad compatibility with **generic PC-class Intel (and similarly PC-like) 32-bit x86** hardware under **Multiboot2**, using **feature-level assumptions** rather than per-SKU tables. No warranty covers every SKU or motherboard; scope expands as we test real machines.

---

## 目录

1. [硬件与兼容范围](#hw-compat-i386)
2. [仓库里有什么](#1-仓库里有什么)
3. [构建与运行](#2-构建与运行)
4. [引导与内存布局](#3-引导与内存布局)
5. [Multiboot2](#4-multiboot2)
6. [物理内存管理 PMM](#5-物理内存管理-pmm)
7. [分页](#6-分页)
8. [内核堆 kmalloc/kfree](#7-内核堆-kmallockfree)
9. [GDT / IDT / 中断](#8-gdt--idt--中断)
10. [PIC 与 PIT](#9-pic-与-pit)
11. [串口与 VGA](#10-串口与-vga)
12. [调度与时间片钩子](#11-调度与时间片钩子)
13. [线程与上下文切换](#12-线程与上下文切换)
14. [用户态、系统调用、ramfs 与进程（已实现）](#13-用户态系统调用ramfs-与进程已实现)
15. [刻意未实现的能力与局限](#14-刻意未实现的能力与局限)
16. [目录结构](#15-目录结构)
17. [许可证](#16-许可证)

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
| **用户态（i386）** | **`int 0x80`**（向量 **128**）、**每进程 PD/CR3**、**内置 ramfs**、**`syscall.c`/`proc.c`/`vfs_ram.c`/`user_bringup.c`/`exec_flat.c`**；详见 **第 13 节**。 |

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
- **Ring 3 / 进程路径**（与「仅恒等映射」正交）：
  - **`paging_clone_kernel_pd()`**：分配新页目录，继承对内核恒等映射的引用；在其上 **挂用户 PTE**。
  - **`paging_map_page(pd_phys, virt, phys, flags)`**：在任意 PD 上建立映射；用户页使用 **PRESENT | RW | **U/S****（见 `proc.c` / `user_bringup.c` / `exec_flat.c` 中常见 **`0x7`**）。
  - **`paging_pte_set_user(virt, true)`**：为已存在的恒等 PTE 打开 **用户可访问**（按需用于身份区中的用户可读页）。
  - **`paging_switch_cr3`** / **`paging_get_cr3`**：进程/试验切换地址空间。
  - **`paging_user_range_ok(va, len, for_write)`**：对 **当前 CR3** 下每一段覆盖的页检查 **Present + US**（写时还要求 **RW**），供系统调用安全访问用户缓冲区。

### 局限

- 身份映射区传统上 **未** 给全部页置 **U/S**；用户可执行区由 **克隆 PD + 专门 `paging_map_page`** 提供。
- **无** NX、**无** PAE；未使用 **PSE** 大型用户页。
- **无** 写时复制（COW）、**无** 匿名缺页扩展（**#PF** 仍 **halt**）、**无** fork 所需的地址空间复制策略。
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

### GDT（`arch/i386/gdt.c` / `gdt_asm.S`）

- 平坦模型：**Ring 0** **代码 / 数据** 段覆盖 **0～4G**。
- **Ring 3**：用户 **代码（0x18）**、**数据（0x23）** 描述符（可执行 / 读写，DPL=3）；供 **`iret`** 与 **`int 0x80`** 返回用户态时使用。
- **386 TSS**：占用 GDT 槽位；**`ltr`** 加载后，特权级切换使用 **`esp0`**（见 **`tss_set_esp0`**，在用户进入 Ring 3 或系统调用路径上配置内核栈顶）。

### IDT（`arch/i386/idt.c`）

- **向量 0～31**：CPU 异常，接入 `exc_asm.S` 桩。
- **向量 32～47**：PIC IRQ，接入 `irq_asm.S` 桩。
- **向量 64**：LAPIC 定时器，`irq_asm.S` 桩 → `sched_timer_tick()` + `lapic_eoi()`。
- **向量 128**：**系统调用**（与 IRQ 同源 **`irq_128`** 路径），门 **DPL=3**，允许 **`int 0x80`**。
- 常规 IRQ/异常门使用 **`0x8E`**；用户可触发向量 **128** 使用 **`0xEE`**（32 位 interrupt gate，DPL=3）。

### 汇编桩

- **`exc_asm.S`**：区分带/不带 error code；`pusha` → 压栈 `verve_exc_frame*` → `verve_exc_dispatch`。
- **`irq_asm.S`**：IRQ 压 vector；同样进入 `verve_irq_dispatch`。

### C 层分发（`kernel/interrupt.c`）

- **`verve_irq_dispatch`**：**`vec == 128`**：**系统调用**，**`syscall_invoke(f)`**（不写 EOI）。**`vec == 64`**：LAPIC 定时器，`sched_timer_tick()` + **`lapic_eoi()`**。**`32 ≤ vec < 48`**：PIC/IOAPIC 向量；若仍用 PIC 8259，`pic_send_eoi`；若启用 LAPIC EOI（定时已迁 LAPIC），对该路径可走 `lapic_eoi`。误用向量会打印并 **halt**。
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

## 13. 用户态、系统调用、ramfs 与进程（已实现）

本节按 **当前 i386 源码** 归纳 **已落地** 能力；**未** 实现或仅够教学用的部分见 **第 14 节**。

### 13.1 引导与 CPU 模式

| 项目 | 实现说明 |
|------|----------|
| **GDT** | **Ring 0** 代码/数据；**Ring 3** 用户 **CS/DS**；**386 TSS** + **`ltr`**（`gdt_asm.S`），**`tss_set_esp0`** 为系统调用/异常提供 **Ring 0 栈顶**（`arch/i386/gdt.c`）。 |
| **IDT 向量 128** | 与 **IRQ 相同** 的入口链，在 **`verve_irq_dispatch`** 中 **特判 `vec==128`** 调 **`syscall_invoke`**；门 **DPL=3**，用户可 **`int 0x80`**（`arch/i386/irq_asm.S`、`arch/i386/idt.c`）。 |
| **进入用户态** | **`verve_enter_user(eip, esp)`**：构造 **iret** 帧（用户 **CS/SS、eip、esp、eflags**）（`arch/i386/user_asm.S`）。 |

### 13.2 分页与地址空间切换

| API / 行为 | 说明 |
|------------|------|
| **`paging_clone_kernel_pd()`** | 分配新 **PD**，与引导页目录 **共享** 内核恒等映射；在其上挂 **用户 PTE**。 |
| **`paging_map_page(pd, va, phys, flags)`** | 在指定 **PD** 上映射；用户页常用 **`PRESENT \| RW \| US`**（代码里常见 **`0x7`**）。 |
| **`paging_switch_cr3` / `paging_get_cr3`** | 切换或查询当前地址空间。 |
| **`paging_user_range_ok(va, len, for_write)`** | 对 **当前 CR3** 校验 **[va, va+len)** 是否落在 **用户可读/可写** 页上（系统调用访问用户指针前使用）。 |

### 13.3 用户虚拟地址约定（冒烟 / `exec_flat`）

以下为 **`user_bringup.c`、`exec_flat.c`、`proc.c`** 使用的 **固定教学布局**（非 ELF PT_LOAD）：

| VA / 区间 | 用途 |
|-----------|------|
| **`0x80000000`** | 用户代码起始页（机器码注入或 **`exec_flat`** 拷贝 ramfs 平面二进制）。 |
| **`0x80001000`～`0x80001FFF`** | 用户栈物理页映射区；**`ESP` 初值常为 `0x80002000`**（栈顶）。 |
| **`0x80002000`** | **初始 program break（brk）**、堆逻辑起点；**`SYS_BRK`** 从此向高地址 **按页** 映射并抬升 **brk**（页对齐）。 |
| 代码页高位偏移 | **`user_bringup`** 在 **≥512** 字节处放 **路径串/说明串**，与指令分区。 |

### 13.4 系统调用一览（`include/verve/syscall.h` + `kernel/syscall.c`）

约定：**`EAX`** = 调用号；参数 **`EBX, ECX, EDX`**；返回值在 **`EAX`**（`syscall_invoke` 写 **`verve_exc_frame::eax`**）。

| 编号 | 宏 | 行为摘要 |
|------|-----|----------|
| **1** | **SYS_WRITE** | **`fd==1`** 时从 **用户缓冲区** 写 **串口**；长度有上限；失败返回负值。 |
| **2** | **SYS_GETPID** | 返回 **`verve_proc_current_pid()`**（单进程 **pid=1**）。 |
| **3** | **SYS_EXIT** | 打日志后 **`cli` + 永久 `hlt`**（**不**实现可继续多道程序的「进程结束」语义）。 |
| **5** | **SYS_OPEN** | 用户态 **路径 VA**；**ramfs** 查找，返回 **fd（≥3）** 或负错误码。 |
| **6** | **SYS_READ** | **fd**、用户 **buf VA**、**len**（有上限裁剪）。 |
| **7** | **SYS_CLOSE** | 关闭 **UFD**。 |
| **45** | **SYS_BRK** | **`ebx==0`**：返回当前 **brk**；否则 **页对齐** 的新 **brk**，在区间内 **映射匿名页**；不允许 **缩小**（当前策略）。 |

### 13.5 进程壳（`kernel/proc.c`）

- **`verve_proc_init_singleton()`**：**克隆 PD**，**pid=1**，记录 **`pd_phys`**，**brk** 初始为 **`0x80002000`**。
- **`verve_proc_exec_reset_brk()`**：平面镜像装入前重置 **brk**（供 **`exec_flat`**）。
- **局限**：无 **进程表**、无 **fork**、无 **线程级 / 用户线程** 与内核 **`thread`** 子系统联动。

### 13.6 内置 ramfs（`kernel/vfs_ram.c`）

- 表驱动：**路径** → **只读数据指针** + **长度**；编译期常量。
- 内置文件示例：**`/hello.txt`**（文本）；**`/exit.bin`**（**9 字节** 平面程序：`SYS_EXIT` + **`int 0x80`**）。
- 用户：**UFD** 表，**fd 自 3 起**；**`copy_user_path`** 配合 **`paging_user_range_ok`**。
- 内核：**`verve_vfs_get_file_k`** 供 **`exec_flat`** 取二进制，不经过用户路径拷贝。

### 13.7 两条用户态演示路径（`kernel/main.c`）

| 符号 / 函数 | 行为 |
|-------------|------|
| **`verve_user_bringup_smoke()`**（**默认**） | 发射机器码：**`BRK(0x80003000)`** → **OPEN `/hello.txt`** → **READ 至堆页** → **WRITE(1)** → **CLOSE** → 横幅 **WRITE** → **GETPID** → **EXIT**。 |
| **`verve_exec_flat_run("/exit.bin")`** | 若将 **`VERVE_USER_SMOKE_BRINGUP` 设为 `0`**（见 `main.c` 注释），走 **平面装入 + iret**。 |
| **二者关系** | **`SYS_EXIT` 当前为 `hlt`**，无法在 **同一次引导** 内先 **bringup** 再 **exec**；需 **编译期二选一** 或通过未来 **可返回的 exit / 多进程** 扩展。 |

### 13.8 源码索引（用户态相关）

| 路径 | 作用 |
|------|------|
| `kernel/syscall.c` | **`syscall_invoke`**。 |
| `include/verve/syscall.h` | 调用号。 |
| `kernel/proc.c`、`include/verve/proc.h` | PD、pid、brk。 |
| `kernel/vfs_ram.c`、`include/verve/vfs_ram.h` | ramfs；**`verve_vfs_open_u` / `read_u` / `close_u` / `get_file_k`** 等。 |
| `kernel/user_bringup.c` | 冒烟机器码。 |
| `kernel/exec_flat.c`、`include/verve/exec_flat.h` | **平面 exec**。 |
| `arch/i386/user_asm.S` | **`verve_enter_user`**。 |

---

## 14. 仍刻意未实现或仅限「够用」的能力

以下与 **第 13 节已实现的 Ring 3/ramfs** 互补：仍 **缺失**、**只做演示**、或 **明显简化** 的能力。

| 领域 | 说明 |
|------|------|
| **多进程 / fork / execve** | 仅 **单例** **`verve_proc`（pid=1）**；**无 fork、无 UNIX execve、无 ELF/解释器**；**`exec_flat`** 仅为 **内置路径 + 平面拷贝 + iret**。 |
| **用户态可持续运行** | **`SYS_EXIT`** 后 **停机**；**无** wait/子进程、**无** init、**无** 从用户态「返回内核主循环」再继续跑其它程序。 |
| **完整虚拟内存** | **无 COW**、**无** 一般匿名 **#PF 缺页处理**（**#PF 仍打印并 halt**）、**无** 共享库映射。 |
| **brk 语义** | **仅页对齐** 扩展；**无缩小**；**brk 区间** 与 **最大堆** 在 **`proc.c`** 中硬编码上限。 |
| **ramfs 以外** | **无** 可写文件系统、**无** 块设备驱动、**无** 持久化存储；**无** `pipe`/`dup` 等 fd 语义。 |
| **真正「硬」抢占** | 定时器只置 **need_resched**；**不在 IRQ 路径里 `cpu_switch`**；需 **checkpoint / yield**（见第 11 节）。 |
| **睡眠 / 阻塞** | **无** 等待队列、**无** 可阻塞锁、**无** 与用户态结合的 **阻塞式 I/O**。 |
| **网络** | 无。 |
| **设备** | 除 **串口轮询 / 定时 / ACPI 表** 等已用路径外，**键盘、磁盘 AHCI/IDE、USB** 等未作产品级驱动。 |
| **SMP 与用户态** | **AP** **TSS/esp0** 与用户态切换 **未** 与 i386 BSP 对齐到同一套「每核 Ring0 栈」策略；**无 IST**；**无** per-CPU 用户态队列隔离。 |
| **AMD64 用户态** | **`make amd64`** 路径 **尚未** 携带与 i386 等价的 **Ring 3 + syscall + ramfs + exec_flat** 全套（以 README 与 `main_amd64.c` 为准）。 |
| **ACPI** | **无** 复杂热插拔运行时（**`acpi_hotplug_stub_init`** 仅为占位）；RSDP 解析 **假设** 表指针在 **低 4G 可映射** 范围。 |
| **电源管理** | 无休眠/关机策略。 |

---

## 15. 目录结构（概览）

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
│   ├── user_asm.S            # verve_enter_user (iret -> Ring 3)
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
│   └── verve/                # acpi_hw、proc、syscall、user、vfs_ram、exec_flat、heap、interrupt、mboot2、paging …
└── kernel/
    ├── main.c                # ACPI / GDT / IDT / LAPIC / 线程 / SMP / 用户态 smoke
    ├── syscall.c             # int 0x80 分发
    ├── vfs_ram.c             # 极简 ramfs（内置文件 + OPEN/READ/CLOSE）
    ├── proc.c                # 最小进程壳（PD + pid）
    ├── user_bringup.c        # Ring 3 冒烟：ramfs 读 /hello.txt 后写串口
    ├── exec_flat.c           # 从 ramfs 装平面用户态镜像并 iret（/exit.bin）
    ├── main_amd64.c          # x86_64 最小 C 入口（与 i386 主线分离）
    ├── acpi_hw.c / acpi_smp.c
    ├── mboot2.c / pmm.c / heap.c
    ├── interrupt.c / sched.c / serial.c / thread.c / smp.c
```

---

## 16. 许可证

若本仓库根目录包含 `LICENSE` 文件，以该文件为准；**若尚未添加**，在补充许可证之前，默认 **All Rights Reserved**（或按组织策略在 GitHub 上选择模板）。建议在仓库设置中明确 **开源许可证** 以便他人合法使用与再分发。

---

## 致谢与参考

- [OSDev Wiki](https://wiki.osdev.org/) — Multiboot、PMM、IDT、PIC 等大量资料。
- Multiboot2 规范 — 引导与信息区格式。

---

*本文档与 **i386 主线上文第 13 节列出的能力** 同步；若实现演进，请同步更新本 README。*
