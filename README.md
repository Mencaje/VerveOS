# VerveOS

一个面向学习与 bring-up 的 **32 位 x86（i386）** 内核实验项目：Multiboot2 引导、物理内存位图分配器、恒等分页、PIC/PIT 定时中断、协作式调度与内核线程演示。代码刻意保持短小可读，**不是**通用桌面或服务器操作系统。

**English (brief):** Minimal educational i386 kernel — Multiboot2, bitmap PMM, identity paging, IRQ0 timer, cooperative scheduling + kernel threads. Single core, ring 0 only, no userspace/syscalls/filesystem.

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
| **架构** | 仅 i386；单核假设；始终 **Ring 0**。 |
| **引导** | Multiboot2；入口校验魔数后调用 `kernel_main`。 |
| **内存** | mmap 驱动的 **4KiB 帧** 位图分配器；内核/Multiboot 信息/位图区 marked used。 |
| **虚拟内存** | **恒等映射**（虚拟地址 = 物理地址）；静态页目录 + 最多 128 张页表。 |
| **堆** | BSS 内约 **256 KiB**，块头对齐；**空闲链表首次适配 + bump**。 |
| **中断** | IDT 填充异常 0–31 与 IRQ 32–47；IRQ0 驱动调度 tick；异常默认打印后 **halt**。 |
| **调度** | 全局 tick、`need_resched`、延后环形队列、嵌套 **preempt_disable**；**不在 ISR 里切换线程**。 |
| **线程** | 静态线程池（最多 8）、环形运行队列、`cpu_switch`、Zombie 栈回收。 |
| **输出** | COM1 串口轮询输出；VGA 文本缓冲 `0xB8000` 少量提示。 |

---

## 2. 构建与运行

### 依赖

- **交叉工具链**：如 `i686-elf-gcc`、`i686-elf-as`、`i686-elf-objcopy`、`i686-elf-ld`（见 OSDev Wiki [GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler)）。
- **GNU Make**。
- **QEMU**：`qemu-system-i386`。

Windows 用户常用 **MSYS2** 安装 make，交叉编译器需单独安装或自行构建。

### 命令

```bash
make              # 生成 build/verve-kernel.elf
make qemu         # qemu-system-i386 -kernel build/verve-kernel.elf -serial stdio -display none
make clean        # 清理 build 与 *.o
```

构建产物：`build/verve-kernel.elf`（Multiboot2 可加载 ELF）。

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

- 命令行、引导器名称、ELF 符号、ACPI RSDP、framebuffer 等 tag **未专门解析或使用**。
- Multiboot2 头里请求了部分信息类型；若需图形/ACPI，需另行解析与驱动。

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
- 门属性固定为项目中使用的 **32 位 interrupt gate** 配置（`0x8E`）。

### 汇编桩

- **`exc_asm.S`**：区分带/不带 error code；`pusha` → 压栈 `verve_exc_frame*` → `verve_exc_dispatch`。
- **`irq_asm.S`**：IRQ 压 vector；同样进入 `verve_irq_dispatch`。

### C 层分发（`kernel/interrupt.c`）

- **`verve_irq_dispatch`**：若 `32 ≤ vec < 48`，视为 IRQ；**IRQ0** 调用 `sched_timer_tick()`，然后 `pic_send_eoi`。其他向量组合若误用会打印并 **halt**。
- **`verve_exc_dispatch`**：串口打印异常号、错误码、EIP/CS；**#14 页故障**额外打印 **CR2**；然后 **永久 halt** —— **不支持** 从异常返回继续执行。

---

## 9. PIC 与 PIT

### 8259 PIC（`arch/i386/pic.c`）

- 初始化级联；**Master 向量基 0x20**、**Slave 0x28**（与 IDT 32–47 对齐）。
- 初始化后默认 **屏蔽所有 IRQ**（`0xFF`）。
- `main` 中 **`pic_irq_unmask(0)`** 仅打开 **定时器 IRQ0**。

### PIT 8254（`arch/i386/pit.c`）

- `pit_set_frequency_hz`：通道 0，约 **1193182/divisor** Hz。
- `kernel_main` 设为约 **50 Hz**。

---

## 10. 串口与 VGA

- **串口**：`kernel/serial.c`，**COM1 `0x3F8`**，轮询发送；**未**编程波特率（依赖固件/QEMU 默认）。
- **VGA**：`kernel/main.c` 直接写 **`0xB8000`** 彩色文本模式；清屏与少量状态行；**无** 通用控制台抽象。

---

## 11. 调度与时间片钩子

实现：`kernel/sched.c`。

### 全局状态

- **`g_tick`**：IRQ0 每次触发递增。
- **`g_need_resched`**：每 `PREEMPT_INTERVAL_TICKS`（**5**）个 tick 置位一次。
- **延后环形队列**：记录 tick（容量 16），满时溢出计数。
- **`g_preempt_disable_depth`**：嵌套临界区；大于 0 时 **`schedule`/`sched_yield`** 路径应直接返回（见 `sched_preempt_disabled`）。

### 重要设计约束

- **`cpu_switch` 不在 IRQ 里执行**：定时器 **只设置标志与延后记录**；线程在 **`sched_checkpoint()`**（或主动 **`sched_yield()`**）里再处理抢占意愿。因此这是 **IRQ 驱动的「抢占意愿」+ 线程内协作切换点**，**不是** 典型的「时钟中断内直接换栈」的全抢占内核。

### 调试输出

- 前若干次 tick 会串口打印；环形队列排空时也会打印 `drained` 等（便于确认 **deferred** 路径在 **线程上下文** 执行）。

---

## 12. 线程与上下文切换

实现：`kernel/thread.c`、`arch/i386/context.S`。

### 数据结构与限制

- 静态数组 **`threads[MAX_THREADS]`**，`MAX_THREADS == 8`。
- **`main_thread`**：作为环形队列头，**`kstack == NULL`**（使用引导栈）。
- 子线程：栈由 **`kmalloc(STACK_SIZE)`**，`STACK_SIZE == 4096`。

### 调度

- **环形链表** 运行队列；**`pick_next`** 只选择 **`THREAD_RUNNABLE`**。
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

## 13. 刻意未实现的能力

以下在本仓库中 **基本不存在** 或 **仅停留在「能继续开发」所需的最低层**：

| 领域 | 说明 |
|------|------|
| 用户态 | 无 Ring 3、无 **syscall/sysenter**、无用户进程/ELF 加载。 |
| 完整虚拟内存 | 无进程地址空间、无 COW、无按需分页、页错误不可恢复。 |
| 真正「硬」抢占 | 时钟不直接换栈；需线程执行到 **checkpoint/yield**。 |
| 睡眠/阻塞 | 无等待队列；无「可阻塞的 mutex」。 |
| 文件系统 / 块设备 | 无。 |
| 网络 | 无。 |
| 键盘/磁盘等 IRQ | PIC 除 IRQ0 外未对外开放示例。 |
| SMP | 无 APIC、无多核调度。 |
| 电源管理 | 无 ACPI 运行时代码。 |

---

## 14. 目录结构（概览）

```
VerveOS/
├── Makefile
├── linker.ld
├── README.md                 # 本文件
├── arch/i386/
│   ├── boot.S                # Multiboot2、_start、内核栈
│   ├── context.S             # cpu_switch
│   ├── exc_asm.S / irq_asm.S # 异常/IRQ 桩
│   ├── gdt.c / gdt_asm.S
│   ├── idt.c
│   ├── paging.c
│   ├── pic.c / pit.c
├── include/
│   ├── arch/i386/            # io、pic、pit
│   └── verve/                # heap、interrupt、mboot2、paging、pmm、sched、serial、spinlock、thread
└── kernel/
    ├── main.c                # 启动顺序与演示总控
    ├── mboot2.c / pmm.c / heap.c
    ├── interrupt.c / sched.c / serial.c / thread.c
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
