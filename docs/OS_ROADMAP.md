# 运行轻量级操作系统的路线（L2 复用 + 功能快进）

本文定义 `rv32ima-sim` 从当前 RV32IM 五级流水原型，扩展到可运行轻量级操作系统
（xv6 级）的实施路线：复用边界、能力底线、两个可验收增量、功能快进模式、
性能约束和验证链。

## 0. 目标与定位

**目标**：在自研周期模拟器上启动一个轻量级操作系统（xv6 级）。

**定位（必须如实声明）**：

- 自研部分：RV32IM 五级顺序流水、CSR/特权级/精确异常、Sv32 MMU、A 扩展。
- 复用部分：设备层（CLINT/PLIC/UART/VirtIO）来自 MARSS-RISCV 上游的 TinyEMU 功能层。
- 授权：MARSS-RISCV 为 MIT 许可（固定提交 `82b3ce2`）。复用需保留上游版权声明，
  不得把上游源码描述为完全自研。

## 1. 复用边界（L2：借设备层，自研核心）

| 组件 | 归属 | 说明 |
| --- | --- | --- |
| 五级流水、译码、执行 | 自研 | 现有 `core/` |
| CSR 文件与 `csrrw/csrrs/csrrc` 语义 | 自研 | 含读写权限位、只读 CSR 拒绝 |
| 特权级 M/S/U、`mstatus/sstatus` | 自研 | |
| 精确异常、`mepc/mcause/mtval/mtvec`、流水冲刷 | 自研 | 五级流水的核心难点 |
| `mret/sret`、`medeleg/mideleg` | 自研 | |
| Sv32 页表遍历、TLB、缺页 | 自研 | 模拟器的架构灵魂 |
| A 扩展、`fence.i` | 自研 | |
| 内核镜像装载（多段 ELF / 裸 bin） | 自研 | 扩展现有 `--bin` |
| CLINT（`mtime/mtimecmp/msip`） | 借用 | 来自 `riscv_machine.c` |
| PLIC | 借用 | 同上 |
| UART NS16550A | 借用 | 同上 |
| VirtIO / HTIF | 借用 | 同上 |
| `PhysMemoryMap`（`iomem.c`/`cutils.c`） | 已 vendor | 现有 `SIM_WITH_MARSS` 路径 |

### 1.1 集成接口：7 函数 shim

设备层通过一个很窄的 CPU 操作面回写状态。只需把这几个操作转发到自研的 CSR/`mip`/
计数器状态，即可把设备接到自研流水线上：

```text
riscv_cpu_get_cycles
riscv_cpu_set_mip / riscv_cpu_reset_mip
riscv_cpu_get_mip
riscv_cpu_get_power_down
riscv_cpu_get_misa
riscv_cpu_flush_tlb_write_range_ram
```

做法：定义 `SimCPUOps`（函数指针结构）承载上述调用，设备注册仍走现有
`phys_mem_map_init` + `cpu_register_ram` + `cpu_register_device`（`system/memory.c` 已在用）。

## 2. 能力底线（任何 OS 都不可省）

| # | 能力 | 验收信号 |
| --- | --- | --- |
| 1 | CSR 文件 + `csrr*` + 权限检查 | 写只读 CSR 触发非法指令 |
| 2 | M/S/U 特权级 | U 模式访问 M CSR 触发异常 |
| 3 | 精确异常 + `mepc/mcause/mtval` | 故障 PC/cause 正确，年轻指令被冲刷 |
| 4 | `mret/sret` + 委托 | M→S 交棒后能正确返回 |
| 5 | 中断 `mie/mip` + 定时器源 | 定时器中断能进入并返回 |
| 6 | Sv32 MMU + TLB + `sfence.vma` | 映射两页并跳转执行 |
| 7 | A 扩展 + `fence.i` | `lr/sc` 失败语义正确 |
| 8 | 内核装载 + 内存映射 | 多段 ELF 可加载 |
| 9 | UART 控制台 | 宿主终端能打出字符 |

## 3. 两个可验收增量

### 增量 A：特权与异常地基

范围：底线 #1 #2 #3 #4 #5 #9。

- CSR 文件 + `csrrw/csrrs/csrrc/csrrwi/csrrsi/csrrci`，含读写权限与只读拒绝。
- 最小 CSR 集：`misa/mhartid/mstatus/mtvec/mepc/mcause/mtval/medeleg/mideleg/mie/mip`、
  `time/cycle/instret`、`mcounteren`。
- 特权级切换与 `mstatus.MPP/SPP/MIE/SIE/MPRV`。
- 陷阱：置 `mepc/mcause/mtval` → 改 `mstatus.MIE→MPIE`、改特权级 → 跳 `mtvec`（direct/vectored）。
- 异常原因：M/S/U 的 `ecall`、非法指令、取指/访存/存地址不对齐、访问故障、断点。
- `mret/sret`；`medeleg/mideleg` 委托。
- CLINT + 中断（借用）：`mtime/mtimecmp/msip`、`mip.MTIP`、`wfi`。
- UART（借用）：控制台输出。

**关键约束**：必须做到精确异常——故障指令之前全部提交，之后全部冲刷（含分支预测器
状态与年轻指令的 MEM/WB 副作用）。

**验收**：裸机程序完成 `M→S 跳转 → 定时器中断 → mret 返回 → 串口打印`；
Spike 逐提交对照定向用例。

### 增量 B：OS 级

范围：底线 #6 #7 #8。

- Sv32：`satp`（MODE=1）、两级遍历（VPN[1]=10b/VPN[0]=10b/offset 12b、PTE 4 字节）、
  R/W/X/U 权限、A/D 位、三类缺页 cause。
- TLB + 冲刷语义（写 `satp` 后、`sfence.vma` 时）。
- 地址翻译接入取指与 load/store 两条路径（取指翻译尤其不能漏）。
- A 扩展：`lr.w/sc.w/amo*.w`（含 `.aq/.rl`）；`fence.i`。
- 装载：ELF 解析（PT_LOAD 多段 + `e_entry` + `.bss` 清零）或维持 `objcopy→bin`。
- 启动约定：M 模式入口、`a0=mhartid`、`a1=DTB`、初始 `sp`。

**验收**：加载多段 ELF 并跑到 `_start`；开 Sv32 页表后跳转执行；
xv6 级内核进 shell（L3 路线则可直接跑 RV32 Linux）。

## 4. 功能快进模式（--fast）

### 4.1 动机

周期精确的成本是本质性的（见第 5 节实测）：跑完整 OS 启动在周期精确下不可行。

### 4.2 设计

两条执行路径：

- **功能路径（--fast）**：不建模流水，按架构语义直推状态（PC、寄存器、CSR、内存、
  特权级、异常）。用于快速推进启动过程。
- **周期精确窗口**：仅在指定区间启用五级流水时序模型。

### 4.3 接口（建议）

```text
--fast                     全局功能快进
--fast-until N             前 N 条动态指令周期精确，之后快进
--trace-window A B         仅对 [A, B) 区间输出周期 Trace
```

与现有 `--stop-pc`、`--max-cycles` 正交。

### 4.4 验收

同一程序在 `--fast` 与周期精确下**架构状态一致**（退休点比对）；
`--fast-until 5000` 能在秒级推进到内核 `main()` 而不产生 GB 级 Trace。

## 5. 性能与 Trace 约束（实测）

在 `build/sim`（flat 后端，紧循环 10,000,003 条退休指令）：

| 场景 | 耗时 | 吞吐 |
| --- | --- | --- |
| 纯模拟（无 trace） | 1.102 s | 约 9.1 M 指令/s（27 M 周期/s） |
| 带 `--trace-json` | 4.998 s | 约 2.0 M 指令/s（慢 4.5 倍） |

Trace 体积：10M 条提交产生 **2.285 GB**，即约 **228 字节/条**。

外推（最乐观上界，真实负载含 MMU/Cache/设备后会更慢）：

| 目标 | 动态指令量级 | 周期精确耗时 | Trace 体积 |
| --- | --- | --- | --- |
| xv6 到 shell | 10^6 – 10^7 | 约 0.1 – 1.1 s | 0.2 – 2.3 GB |
| RV32 Linux 到 shell | 10^8 – 10^9 | 约 11 – 110 s | 23 – 228 GB |

结论：**xv6 量级周期精确可行，Linux 量级不现实**。因此必须：

1. 提供功能快进（第 4 节）。
2. Trace 受限：仅 `--trace-window` 或前 N 条；或改用紧凑二进制 + 写增量格式。
3. 功能验证走快进路径，时序只在抽样窗口观察。

## 6. 验证链

| 阶段 | 参考模型 | 用法 |
| --- | --- | --- |
| 增量 A（CSR/特权/异常/中断） | Spike（`--isa=rv32ima_zicsr`） | 扩 `programs/diff_smoke.S`，加定向用例 |
| 增量 B（Sv32/A 扩展/装载） | Spike + QEMU（`qemu-system-riscv32`） | 同镜像双跑，比对串口与关键状态 |
| 系统级 bring-up | QEMU riscv32 | 启动日志逐段比对 |
| 随机覆盖（后期） | riscv-arch-test / riscv-tests | 补充 |

每个增量都必须：先跑通、再对照、再提交（沿用现有 `spike_compare.py` 模式）。

## 7. 内存映射与启动约定

对齐 QEMU virt 布局，避免改 OS 侧 `memlayout.h`：

| 区域 | 基址 |
| --- | --- |
| M 模式复位 | 0x00001000 |
| CLINT | 0x02000000 |
| PLIC | 0x0C000000 |
| UART0 | 0x10000000 |
| VIRTIO | 0x10001000 |
| DRAM | 0x80000000 |

## 8. 里程碑顺序

```text
M1  CSR + 特权级骨架                                  [已完成]
M2  精确异常 + mret/sret + 流水冲刷        <- 最难
M3  CLINT(借用) + 中断 + 委托
M4  UART(借用) 控制台
M5  功能快进 --fast + 受限 Trace
M6  Sv32 MMU + TLB + 缺页
M7  A 扩展 + fence.i
M8  多段 ELF 装载 + 启动约定
M9  PLIC(借用) + VirtIO(借用)
M10 xv6 级内核 bring-up（与 QEMU 联调）
```

M1–M4 构成增量 A，M5 是性能基础设施，M6–M8 构成增量 B，M9–M10 达到 OS 级。

**进度**：M1 已完成（`src/riscvsim/core/csr.{h,c}` + M/S/U 特权级骨架）。
验证：CTest 6/6 通过；与 Spike 实测对照 CSR 读-改-写程序 7/7 一致。
分支 `feat/csr-privilege-skeleton`。

## 9. 风险与不做的事

- **精确异常与流水冲刷**是最大 bug 来源：故障指令的提交边界、分支预测器恢复、
  年轻指令的 WB 副作用必须一并处理。
- **阻抗失配**：只借设备层时，设备依赖的 CPU 操作面需 shim 到位；不要引入第二套
  PC/内存模型（那会退化为 L3，失去自研定位）。
- **不做**：乱序核、多核一致性、FPGA 上板、网络/图形设备；Cache/DRAM 时序模型可后置。
- **定位风险**：一旦复用面扩大，需同步更新 `docs/UPSTREAM.md` 式声明，保持描述诚实。
