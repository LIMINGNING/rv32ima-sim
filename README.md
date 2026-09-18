# rv32ima-sim

面向处理器设计学习与调试的 C 语言周期模拟器。项目计划实现单核、单发射、顺序执行的 RV32IMA 五级流水，配套 Python 轨迹工具，并逐步完善指令验证与系统运行能力。

目前处于 **单 hart、同步 RAM 的 RV32IMA 五级流水原型** 阶段，能够运行内置指令数组，观察逐拍执行过程并运行定向测试。系统级运行和 Linux 启动仍是后续目标。

新增最小增量：支持外部 `.bin` 装载、JSONL 提交/异常 Trace，以及可选的
MARSS/TinyEMU物理地址映射后端（真实链接其 `iomem.c`，可访问RAM和测试MMIO设备）。
已提供与 Spike 运行相同程序的 33 条退休事件差分脚本。
构建、命令、接口边界和验证方法见 [docs/INCREMENT_1.md](docs/INCREMENT_1.md)。

## 当前状态

- **流水核心**：实现 IF / ID / EX / MEM / WB 五级流水，各级计算下一拍状态后统一更新；支持数据相关停顿与分支、跳转冲刷。目前没有数据转发，相关指令需要等待写回。
- **指令执行**：支持 RV32I 整数运算、条件分支、跳转、加载与存储，以及同步 RAM 模型下的 FENCE；支持 M 扩展全部 8 条乘除法指令。M 指令使用非流水化 EX 功能单元，默认乘法 3 拍、除法/取余 32 拍，运行前可配置延迟；忙时保持 ID/IF。详见 [乘除单元时序模型](docs/multiply-divide.md)。
- **A 扩展**：支持 9 条 AMO.W 和 LR.W/SC.W；阻塞式原子访存单元在 MEM 分拍运行：AMO 读/运算/写共 3 拍，LR 1 拍，SC 检查/条件写共 2 拍。MEM 忙时保持 EX/ID 和取指 PC；接受全部 aq/rl 组合，当前串行访存提供更强排序。详见 [A 扩展设计与增量记录](docs/atomic-pipeline.md)。
- **内存与提交**：原数组模式保留；外部镜像模式（`--bin`）通过统一映射 RAM 装载，可选用 MARSS/TinyEMU 物理地址映射后端。指令数组和数据 RAM 分离，数据按小端访问。总线 load/store 的副作用延迟到 WB，MEM 只做无副作用检查。原子操作到达 MEM 时较老指令已提交，原子写入在 MEM 最后一拍生效，WB 只写寄存器和退休；x0 保持为零。
- **提交 Trace 与差分**：`--trace-json` 输出每行一个 JSON 对象的提交/异常记录（含寄存器与内存写回），配合 `tools/spike_compare.py` 以 Spike 为参考模型做正常提交的架构状态差分。
- **异常报告**：支持非法指令、访问越界、地址不对齐、ECALL 和 EBREAK。异常指令到达 WB 时记录原因、PC 与故障值并停止模拟，不计入退休数；尚无 CSR、特权级切换或异常处理程序。
- **观测与测试**：记录周期数、退休数、数据相关停顿、EX 乘除等待拍数、MEM 阻塞拍数和重定向次数；提供 RV32IM/A 定向测试（含逐拍状态断言），以及阶段日志转换、比较和 HTML 展示工具。

无参数时程序仍由 `src/riscvsim/riscvsim_cpu.c` 中的固定指令数组提供，数据RAM从地址0开始；`--bin`模式默认映射1MiB RAM至0x80000000。模拟器在程序末尾流水排空、指定停止PC退休、发生异常或耗尽周期预算时停止。无冒险的N条普通指令需要N+4拍完成。

目前尚未支持 ELF 装载、CSR、FENCE.I、MMU 及完整系统设备、缓存或可变访存延迟，也尚未接入 HDL 验证。原子操作目前只面向单 hart 独占 RAM，不支持多个核心或 DMA 并发修改同一 RAM。原 `tools/diff.py` 仍比较阶段事件；新增 `tools/spike_compare.py` 以 Spike 为参考模型差分正常提交的寄存器与内存写结果。

## 构建与运行

需要 CMake 3.16+ 和支持 C11 的 C 编译器；轨迹工具与对应测试还需要 Python 3，仅使用标准库。

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 默认运行 AMO + LR/SC 演示，输出逐拍轨迹与周期、退休数汇总
# 7 条指令，20 拍；数据从 10 变成 14
./build/sim

# 运行回归测试；配置时找到 Python 3 会同时注册轨迹工具测试
ctest --test-dir build --output-on-failure
```

生成可浏览的流水轨迹：

```bash
mkdir -p out
./build/sim > out/trace.log
python3 tools/view_trace.py out/trace.log -o out/trace.html

```

用浏览器打开 `out/trace.html`。日志导出与比较方法见 [轨迹工具说明](tools/README.md)。

## 目录结构

```text
rv32ima-sim/
├── CMakeLists.txt   # 构建与测试入口
├── src/riscvsim/    # CPU 状态、五级流水、译码、执行与轨迹输出
├── tests/          # RV32IM/A 定向测试与 Python 工具测试
├── docs/           # 设计说明、时序模型、验证方法与系统运行路线
├── tools/          # 日志转换、轨迹比较与 HTML 展示
└── marss-riscv/    # 参考源码，不参与当前 CMake 构建
```

`build/` 和 `out/` 分别用于本地构建产物与轨迹输出。

## 后续目标

1. **完善流水与程序运行**：加入数据转发、可配置访存等待，补齐相关测试与统计信息。
2. **完善执行时序**：在现有 A 扩展状态机上逐步加入可变延迟与内存请求接口；进一步研究流水化乘法器、迭代除法器及完成结果仲裁。引入其他访存主体前实现共享端口仲裁和保留失效通知。
3. **完善验证链路**：扩展已有 Spike 正常提交差分，补齐异常与系统事件的语义标记；逐步加入随机测试，并与自研 HDL CPU 在共同配置下逐拍对照。
4. **探索系统级运行**：逐步实现 M/S/U 特权级、异常中断、Sv32 地址转换及必要外设，最终争取由周期模型启动 Linux 并进入 shell。

预期形成一个便于理解、测试和定位流水问题的模拟平台，包含 C 周期模型、Python 轨迹与验证工具，以及可复现的测试和运行示例。先保证基础指令与流水行为可靠，再逐步推进 HDL 对照和 Linux 系统运行。
