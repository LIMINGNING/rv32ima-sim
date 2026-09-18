# 最小增量：程序装载、提交 Trace、MARSS 系统接口试接

本增量保留原来的 RV32IM 译码/执行和 current/next 五级流水。
不实现 A、CSR、分页、中断、Linux；无参数 `sim` 仍运行原内置演示。

## 1. 构建与运行

独立 RAM 后端（不依赖 MARSS）：

```bash
cmake -S . -B build-flat -DCMAKE_BUILD_TYPE=Debug
cmake --build build-flat -j
ctest --test-dir build-flat --output-on-failure
```

可选的 MARSS/TinyEMU 物理地址映射后端：

```bash
cmake -S . -B build-marss -DCMAKE_BUILD_TYPE=Debug \
  -DMARSS_SOURCE_DIR=/home/li/cpu/third_party/marss-riscv/src
cmake --build build-marss -j
ctest --test-dir build-marss --output-on-failure
./build-marss/sim --help
```

`--bin FILE` 加载小端 RV32IM 原始二进制，默认入口/装载地址为 `0x80000000`，
默认 RAM 为从该地址开始的 1 MiB。指令与数据使用同一 RAM。
`--base` 与 `--ram-size` 必须按 4096 字节对齐；RAM 最大 256 MiB。
镜像非空、长度必须是 4 的倍数。当前不支持 ELF、压缩指令或跨装载范围执行。
镜像末尾作为测试环境的排空标记；混有数据的镜像应显式指定 `--stop-pc`。
`--stop-pc` 表示该 PC 的指令成功退休后停止；它不是 ISA 指令。

```bash
./build-marss/sim --bin program.bin --trace-json result.jsonl \
  --backend marss --max-cycles 100000 --stop-pc 0x80000080
```

退出码：0 完成，1 架构异常，2 参数/文件错误，3 周期预算耗尽。
超时不算成功，不生成虚假的退休事件。`--stage-trace` 可额外输出原阶段日志。

## 2. 提交接口

每行一个 JSON 对象。`commit` 在成功写回/写存储器之后输出；异常输出 `trap`，
不增加退休数，不带有效寄存器或内存写。字段：

| 字段 | 含义 |
| --- | --- |
| type | commit / trap |
| cycle | 从 1 开始的目标周期 |
| seq | 截至该事件的正常退休数；trap 不增加它 |
| pc, insn | 指令地址与 32 位机器码 |
| wen, rd, wdata | 整数寄存器写回；x0 的 wen 为 0 |
| mem_addr | load/store 有效地址（字节地址） |
| mem_wmask | 相对于 mem_addr 的有效字节掩码：SB=1、SH=3、SW=15 |
| mem_wdata | 仅有效字节的数据，窄写的高位清零 |
| trap, cause, tval | 是否异常、RISC-V 异常编号、故障附加值 |

不要求与 MARSS 周期相同。当前比较正常退休结果，未验证 CSR、原子和中断差分。
MARSS 现有 trap cause 是内部模拟器异常编号，不能直接当作 RISC-V cause 比较。

## 3. 系统层接到了哪里

`SimBus` 定义无副作用的 `probe`、读取和写入三个物理访问回调。
装载程序通过总线取指，MEM 检查访存地址，WB 执行有副作用的总线 load/store。
将总线读取推迟到 WB 是为了防止较老指令尚未确认成功时，年轻 MMIO 读取消耗设备状态。
这定义了当前同步访存模型的提交行为；后续加入异步请求和等待需重新明确握手协议。

`marss` 后端直接编译上游 `iomem.c` 与 `cutils.c`，使用其
`phys_mem_map_init`、`cpu_register_ram`、`get_phys_mem_range`、
`cpu_register_device` 和设备回调机制。通过映射查找到实际 RAM 或设备后访问。
保留上游文件和许可证原样，不复制改名冒充自研代码。

为验证副作用，在 `0x10000000` 映射了一个**自写测试锁存器设备**，仅允许32位读写；
它不是 MARSS UART。统计 device_reads/device_writes 用于检查访问次数。
程序不能从 MMIO 取指，错误路径设备访问和被异常取消的设备访问均不得执行。

已接通的范围：**自写流水 → 总线回调 → MARSS/TinyEMU物理地址映射 → RAM/测试设备**。
未接通的范围：`riscv_machine.c` 完整机器、TinyEMU CPU状态、CSR、Sv32、PLIC/CLINT、
真实UART/VirtIO、Linux。物理总线后端可用不等于已经具备这些系统能力。

## 4. 与 Spike 做差分

差分脚本 `tools/spike_compare.py` 以 Spike 为参考模型，验证正常提交结果的架构
状态一致性。先保证 `~/cpu/build/spike/spike` 可用，然后运行：

```bash
python3 tools/spike_compare.py
```

脚本编译同一 `programs/diff_smoke.S`，DUT 运行其 `.bin`、Spike 运行同一 ELF，
测试整数运算、RAM字/半字/字节读写、符号扩展、8条M指令、相关、分支循环和跳转。
在 `test_done` 处结束 DUT；Spike 从复位桩启动，逐条比较 33 条测试提交事件，
并校验复位 ROM 的 5 条启动 PC（不做静默丢弃）。比较 PC、指令、寄存器写回、
load 地址与内存写；末尾故意破坏一个写回值，确认比较器能识别错误。
详见 [docs/SPIKE.md](SPIKE.md)。

本次结果：33 条提交匹配；已有 CPU 与轨迹测试通过；新增装载/异常/MMIO 测试通过；
Spike 差分与负向检查通过。只证明这组定向测试，不代表完整 RV32IM 认证或 Linux 支持。

## 5. 下一阶段

先扩展裸机用例和总线测试，再明确特权状态的所有权、异常入口与返回、设备中断及
定时器推进接口；若移植TinyEMU的MMU/设备，需要使它们与自写核心共享一致的寄存器、
CSR、访存和时间状态。不能启动两个互不相干的CPU然后称为系统层接入。
