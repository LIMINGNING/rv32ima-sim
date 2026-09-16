# M 扩展：可配置的多周期 EX

## MARSS-RISCV 实际模拟的结构

本次阅读项目内 `marss-riscv/` 的顺序核心实现：

| 参考文件 | 实现 |
|---|---|
| `src/riscvsim/core/inorder.c` | 分配 `imul[]`、`idiv[]` 功能单元流水级数组 |
| `src/riscvsim/utils/sim_params.c`、`.h` | 配置 `num_mul_stages`、`num_div_stages`、每级 latency；本地版本默认级数和每级延迟都是 1 |
| `src/riscvsim/core/inorder_frontend.c` | 将指令派发到目标单元，检查入口是否空闲；`enable_parallel_fu` 控制单元并行策略 |
| `src/riscvsim/core/inorder_backend.c` | `in_core_execute_pipe()` 用已过周期数和每级延迟控制推进，从末级向首级更新；末级等待 MEM 和派发顺序队列允许其输出 |
| `src/riscvsim/decoder/riscv_isa_execute.c` | 使用 C 运算计算 M 指令的功能结果与边界情况 |

MARSS 的 `stage_exec_done` 防止指令在同一功能单元阶段等待时重复进行功能计算。
这是一种功能行为和硬件时序分离的建模方式：描述流水级、占用、延迟、转发与阻塞，不模拟门电路传播，也没有逐位展开 Booth、Wallace tree 或恢复余数除法算法。
配置更多级可表达流水化单元；级数为 1、级延迟大于 1 可表达非流水化多周期单元。

上游参考：[MARSS-RISCV 顺序核心](https://github.com/bucaps/marss-riscv/blob/master/src/riscvsim/core/inorder_backend.c)。

## 本次增量的选择

采用非流水化多周期功能单元，共享原有单个 EX 槽。任何时刻只执行一条 EX 指令，不引入多个并行 FU、转发总线或完成队列。
功能结果在首次 EX 推进时计算并保存在锁存器内，但必须等待配置周期数后才能进入 MEM；只有 WB 可以修改目标寄存器。

默认延迟是本项目选择，**不是 MARSS 默认值、ISA 要求或某个真实乘除电路的测量结果**：

| 指令 | EX 总延迟 | 额外 EX 等待 | 单条指令从 IF 到 WB |
|---|---:|---:|---:|
| MUL/MULH/MULHSU/MULHU | 3 | 2 | 7 拍 |
| DIV/DIVU/REM/REMU | 32 | 31 | 36 拍 |
| 普通整数 ALU | 1 | 0 | 5 拍 |

除零、带符号除法溢出等情况也使用相同的除法延迟，没有提前完成或 DIV/REM 结果复用。
独立的两条乘法也必须串行占用 EX；3 拍延迟不意味着可以每拍发射一条。

```text
默认 MUL：IF → ID → EX1 → EX2 → EX3 → MEM → WB
                    └──保持 ID/IF──┘
```

## 状态和控制

- `INCore.mul_cycles/div_cycles`：运行前配置的 EX 总延迟。
- `InsnLatch.m_result_ready`：当前 M 指令的功能结果已经计算，与剩余延迟一起随指令保存。
- `InsnLatch.ex_cycles_left`：完成前还需多少次 EX 推进。
- `execute_stalled`：本拍 EX 未完成，保持 ID 和 fetch_pc。
- `execute_stalls`：EX 延迟导致的等待拍数；不重复计入数据冒险 `stalls` 或 `memory_stalls`。

EX 尚未完成时，更新后的指令保留在 `next_execute`，不发出 `next_memory`。
已在 MEM/WB 中的较老指令继续推进，不能因 EX 等待重复执行或退休。
完成的那拍允许 ID 开始处理下一条指令；数据依赖仍按现有无转发模型等待 WB。

MEM 阻塞优先于 EX：较老 AMO 占用 MEM 时，整个 EX 的推进使能关闭，M 指令的计数器也保持。这是本项目的保守时钟使能选择；不模拟独立运行并在输出端等待的并行乘除单元。
本实现是 MARSS 可配置 FU 思想的子集，不等价于 MARSS 任意流水化/并行配置。

更老的异常阻止调用 EX；分支丢弃的 M 指令不启动。中途耗尽 `in_core_run()` 周期预算时，锁存器保留状态，下一次调用继续等待，不重新计算。

## 配置方法

在 `in_core_init()` 之后、第一次 `in_core_run()` 之前调用：

```c
if (!in_core_set_m_latency(&core, 3, 32)) {
    /* 配置无效：两个延迟必须非零，而且模拟尚未开始。 */
}
```

延迟为 1 可恢复原来的单拍 EX 行为；无效调用返回 0 且不修改配置，成功返回 1。
不要在运行中直接修改配置字段。重新初始化会恢复默认 3/32 拍并清空中间状态与统计。

## 验证与后续增量

原有 M 运算向量继续检查全部 8 条指令的结果，并增加默认周期数检查。
逐拍测试覆盖 1/2/5/32 拍配置、ID/PC 保持、结果不能提前写回、连续独立乘除、RAW 依赖、AMO 反压、所有预算切分点恢复，以及前后异常和跳转。
原有 A 扩展和默认演示仍保持原来的周期数。

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

下一步可先选定一个明确的硬件结构，例如三级可流水化乘法器，增加每级 valid、入口 ready 和顺序输出仲裁，并分别测试延迟与吞吐率。
若目标是逐拍研究除法算法，则应另加余数、商和迭代计数寄存器，每拍迭代；不能把当前固定 32 拍等待解释为已经实现了逐位除法器。
