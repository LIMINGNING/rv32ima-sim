# Spike 差分

`tools/spike_compare.py` 以 [Spike](https://github.com/riscv-software-src/riscv-isa-sim)
为参考模型，对自写周期模拟器做架构状态差分：两边执行同一测试程序，逐条比较正常
提交（commit）事件的寄存器写回、内存写与 load 地址，从而验证指令语义的正确性。

## 依赖

- `spike`：`~/cpu/build/spike/spike`（可用 `--spike` 指定）
- RISC-V 交叉工具链：`riscv64-unknown-elf-gcc / objcopy / nm`（`--cross` 指定前缀）
- 自写模拟器：默认 `~/cpu/build/rv32ima-increment/sim`（`--sim` 指定）
- 可选：`~/cpu/build/spike-deps` 下的 `dtc`（脚本会自动加入 PATH/LD_LIBRARY_PATH）

## 用法

```bash
python3 tools/spike_compare.py
```

默认后端 `flat`，可加 `--backend marss` 切换到 MARSS 物理地址映射后端。其他参数：

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `--sim` | `~/cpu/build/rv32ima-increment/sim` | 被测模拟器 |
| `--spike` | `~/cpu/build/spike/spike` | Spike 参考模型 |
| `--out` | `out/spike-compare` | 产物目录 |
| `--cross` | `riscv64-unknown-elf-` | 交叉工具链前缀 |
| `--backend` | `flat` | `flat` 或 `marss` |

## 工作原理

1. 编译 `programs/diff_smoke.S` 生成 ELF，再用 `objcopy` 转成 DUT 可加载的 `.bin`。
2. 用 `nm` 定位 `test_done` 符号，作为 DUT 的停止 PC。
3. DUT 运行 `.bin` 并输出 `--trace-json` 提交事件。
4. Spike 以 `--log-commits` 运行同一 ELF，`--instructions` 固定为 `5 + 33`
   （5 条复位 ROM 启动指令 + 33 条测试指令），不依赖 DUT 的 trace 条数。
5. 严格解析 Spike 的 commit 日志，校验复位 ROM 的 5 条启动 PC
   （`0x1000..0x1010`），不做静默丢弃；逐条比较 33 个提交事件。
6. 比较字段：PC、指令、寄存器写回（`wen/rd/wdata`）、load 地址（`mem_addr`）、
   内存写（`mem_wmask/mem_wdata`，含 SB/SH/SW 的字节掩码归一化）。
7. 末尾做负向检查：故意翻转一个写回值，确认比较器能识别错误。

## 输出

产物目录 `out/spike-compare/` 下包含：

| 文件 | 内容 |
| --- | --- |
| `smoke.elf` / `smoke.bin` | 测试程序 ELF 与二进制镜像 |
| `dut.jsonl` | DUT 提交事件 |
| `spike-commit.log` | Spike 原始 commit 日志 |
| `reference-commit.jsonl` | 解析后的参考提交事件 |
| `spike-console.log` | Spike 控制台输出 |
| `run.json` | 本次运行参数与结果摘要 |

成功时打印 `SPIKE DIFFTEST PASS: 33 retirement events matched`。

## 局限

只覆盖 33 条定向测试的正常退休事件，未验证 CSR、原子、异常/中断、特权级切换。
不能作为完整 RV32IM 认证或 Linux 支持的证据。

单元测试见 `tests/spike_tools_test.py`（已注册到 CTest 的 `spike_trace_tools`）。
