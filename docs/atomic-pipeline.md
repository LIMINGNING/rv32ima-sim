# RV32A：阻塞式原子访存单元

本增量在原有单发射、顺序五级流水上实现 RV32A 的全部 11 条 `.W` 指令。
目标是把原子操作的读取、运算和写入映射到明确的模拟周期，并验证流水线反压。
当前不建模缓存、互连、多个 hart、DMA 或异步中断；固定周期数是本模型的微架构选择，不是 ISA 要求，也不是对某颗真实 CPU 的性能标定。

## 参考 MARSS-RISCV 的哪些部分

本次阅读了项目内 `marss-riscv/` 中的以下实现（参考目录不参与构建）：

- `src/riscvsim/decoder/riscv_isa_decoder.c` 的 `ATOMIC_MASK` 分支：按 funct5 区分 LR、SC、AMO，并识别读写源寄存器。
- `src/riscvsim/core/inorder_backend.c` 的 `in_core_memory()`：MEM 用 `stage_exec_done`、`elasped_clock_cycles`、`max_clock_cycles` 跟踪访存执行；有未完成的内存请求时保留指令，完成后再送往提交级。
- `src/riscvsim/core/riscv_sim_cpu.c` 的 `mem_cpu_stage_exec()`：访存功能执行与缓存/内存延迟建模分离，分别考虑原子操作的读、写延迟。
- `src/riscvsim/memory_hierarchy/temu_mem_map_wrapper.c` 的 `temu_exec_atomic_insn()`：通过 TinyEMU 内存接口完成原子指令的功能行为。

上游：[MARSS-RISCV](https://github.com/bucaps/marss-riscv)、[顺序核心说明](https://marss-riscv-docs.readthedocs.io/en/latest/sections/incore-microarch.html)。
指令语义：[RISC-V A 扩展规范](https://docs.riscv.org/reference/isa/unpriv/a-st-ext.html)。

本项目采用“MEM 保持请求直到完成，再进入 WB”的结构思想。MARSS 的上述路径会先执行 TinyEMU 功能操作，再模拟相应延迟；本项目使用更小的显式状态机，把实际数据读取、运算和写入分别放在对应拍。
没有移植 MARSS 的缓存、MMU、转发网络或内存控制器。

## 本次开发的三个增量

1. **AMOSWAP.W 通路**：增加 opcode、地址锁存器、三拍 MEM 和反压；逐拍检查写入时机及前端保持，回归原有 RV32IM 测试。
2. **其他 8 条 AMO**：复用通路，只扩展运算；测试溢出、符号边界、寄存器别名及相邻访存。
3. **LR/SC**：增加每 hart 的保留状态与两拍 SC；补齐失效、异常、错误路径、预算中断后恢复、演示和文档。

三个增量分别构建和运行了回归测试，代码没有引入为未来缓存系统预设的复杂接口。

## 各阶段的职责

| 阶段 | A 指令的工作 |
|---|---|
| IF | 正常读取 32 位指令 |
| ID | 检查 `.W`、funct5、LR 的 rs2=0；等待源操作数，设置 writes_rd |
| EX | 将 rs1 值保存到 mem_addr；A 指令没有地址立即数 |
| MEM | 检查地址，执行原子访存状态机 |
| WB | 将 result 写回 rd，计入退休数；不重做原子内存操作 |

`InsnLatch.mem_addr` 保存地址，`result` 保存内存旧值或 SC 状态，`mem_value` 保存新值。
`atomic_phase` 随指令保持，不借用宿主机执行时间，也不会每拍重新读取或重复发起同一操作。

## 固定时序

| 指令 | 第一次 MEM | 第二次 MEM | 第三次 MEM | 单条指令从 IF 到退休 |
|---|---|---|---|---|
| AMO.W | 检查地址、读取旧值 | 运算生成新值 | 写内存、发送 WB | 7 拍 |
| LR.W | 检查地址、读取、建立保留、发送 WB | — | — | 5 拍 |
| SC.W | 检查地址、检查并消费保留 | 条件写入、发送 WB | — | 6 拍 |

SC 成功和保留检查失败均占两拍；失败返回 1 且不写内存。地址错误在第一次 MEM 产生异常，下一拍 WB 报告，因此没有后续写入阶段。

一条无依赖 AMO 的时序：

```text
周期       1    2    3      4        5        6      7
阶段      IF   ID   EX   MEM读   MEM运算   MEM写    WB
内存修改   否   否   否     否       否       是     否
rd修改     否   否   否     否       否       否     是
```

AMO 的前两次 MEM 和 SC 的第一次 MEM 设置 `memory_stalled`。
调度器保持 EX、ID 和 fetch_pc，不执行这些级；MEM 保存其更新后的锁存器，`next_commit` 保持为空。
原来的 WB 指令仍可在该拍完成一次，之后不会因为 MEM 等待被重放。
MEM 最后一拍可以放行 EX 中的下一条指令。

`memory_stalls` 统计 MEM 未完成而阻塞流水的拍数：每条 AMO 增加 2，每条 SC 增加 1，LR 增加 0。
原有 `stalls` 仍只统计寄存器数据冒险等待，不与 MEM 阻塞重复计数。

## 为什么允许 MEM 修改内存

每拍先调用 WB，再调用 MEM。这个顺序流水只有一个 MEM 槽，所以 A 指令开始 MEM 时，较老指令已经提交；若较老指令在 WB 报告异常，该拍不会调用 MEM。
更老分支已经在 EX 决定路径。MEM 中的 A 指令因此已经不可被更老指令取消，可以在数据端口完成时写入，下一拍 WB 再写寄存器和退休。

MEM 忙时所有年轻指令都无法到达数据端口，所以同一个 hart 的访问无法插入 AMO 的读改写之间。预算耗尽只暂停推进周期，保留全部中间状态，恢复后继续。

这不是跨宿主线程的内存锁。禁止通过多个 `INCore` 共享 RAM 或由外部并发写 RAM 来推断多 hart 原子性；后续需要在共享内存请求层实现仲裁、原子事务和写入失效通知。
加入异步中断、调试取消或晚到的访存错误时，也必须重新审查上述不可取消条件。

## LR/SC 保留规则

- 每 hart 仅有一份保留：`reservation_valid` 和 `reservation_addr`。
- 保留范围为对齐的 4 字节；LR 读取成功时建立，新 LR 覆盖旧保留。
- SC 先检查访问地址，再检查保留；有效且地址相同则成功，返回 0，否则返回 1。
- SC 完成检查后立即消费保留，无论成功或失败；写入与检查之间独占本 hart 的 MEM。
- 本实现选择让普通 SB/SH/SW 或 AMO 对保留范围中任一字节的写入都使保留失效，即使写入值没有变化。非重叠写入不失效。
- 地址异常的 LR/SC 在检查阶段直接结束，没有读取、写入或保留修改。无保留的 SC 也执行地址检查，不用“失败”掩盖地址错误。
- 初始化清空保留；被分支丢弃的 LR/SC 不会进入 MEM，因此不影响保留。

## aq、rl 和非法编码

译码使用 bits[31:27]，保留在 `insn` 中的 aq/rl 不会混入 funct5。
四种 aq/rl 组合全部接受。当前所有访存顺序推进，原子操作阻塞数据端口；因此当前实现提供比各组合要求更强的顺序，四种组合具有相同延迟。
这不是对多 hart 弱内存行为的验证，也不包括 I/O 域。今后引入访存重排或 MMIO 必须重新处理排序。

只接受 RV32A 的 `.W` 和 11 种 funct5；`.D`、其他宽度、未知 funct5、rs2 非零的 LR 都报告非法指令。
LR 使用 load 未对齐/访问故障原因 4/5；SC 和 AMO 使用 store/AMO 原因 6/7。
异常沿用本项目内部 `cause + 1` 编码，到 WB 记录并停止，不计入退休数。

## 运行和查看轨迹

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
mkdir -p out
./build/sim > out/atomic.log
python3 tools/view_trace.py out/atomic.log -o out/atomic.html
```

演示依次运行 AMOADD、LR、ADDI、SC 和普通 load（含两条初始化指令），7 条指令共 20 拍。
RAM[16] 从 10 变为 13，再变为 14。x3 返回 AMO 旧值 10，x5 返回 SC 成功状态 0，x6 读取最终值 14。

轨迹会在连续三拍记录同一条 AMO 的 MEM 事件，在两拍记录同一条 SC 的 MEM 事件。
日志记录阶段执行事件；被 MEM 反压而仅保持的 EX/ID 不产生执行事件，不能把 HTML 中没有事件的格子理解成锁存器为空。
具体保持状态和各拍副作用由 `tests/rv32a_test.c` 的逐拍断言检查。

下一步可以先为同步 RAM 请求增加可配置响应延迟，验证请求只发起一次、响应前保持流水；之后再考虑缓存和多访问者，不应直接把当前三拍当作真实缓存访问延迟。
