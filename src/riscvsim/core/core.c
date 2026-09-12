#include "core.h"
#include "../riscvsim_cpu.h"
#include "../utils/trace.h"
#include <string.h>

void in_core_init(INCore *core, RISCVSIMCPUState *cpu,
                  const uint32_t *program, size_t count, uint32_t base,
                  uint8_t *data, size_t data_size)
{
    memset(core, 0, sizeof(*core));
    memset(cpu, 0, sizeof(*cpu));
    core->simcpu = cpu;
    core->program = program;
    core->program_size = count;
    core->program_base = core->fetch_pc = base;
    core->data = data;
    core->data_size = data_size;
    core->fetch_done = count == 0;
}

static int drained(const INCore *core)
{
    return core->fetch_done &&
           !core->decode.has_data &&
           !core->execute.has_data &&
           !core->memory.has_data &&
           !core->commit.has_data;
}

void in_core_run(INCore *core, uint64_t cycles)
{
    if (core->halted || drained(core))
        return;

    for (uint64_t i = 0; i < cycles; ++i)
    {
        int stop = in_core_run_5_stage(core);
        core->simcpu->clock++;
        if (stop)
            break;
    }
}

int in_core_run_5_stage(INCore *core)
{
    core->next_decode = core->next_execute = (CPUStage){0};
    core->next_memory = core->next_commit = (CPUStage){0};
    core->stalled = core->redirected = 0;
    if (!in_core_commit(core))
    {
        in_core_memory(core);
        /* 异常随指令到 WB 报告，较老的指令先完成；丢弃年轻指令。 */
        if (!core->next_commit.latch.exception)
        {
            in_core_execute(core);
            if (!core->next_memory.latch.exception && !core->redirected)
            {
                trace_stage(core, "ID", core->decode);
                in_core_decode(core);
                if (!core->next_execute.latch.exception && !core->stalled)
                    in_core_fetch(core);
            }
        }
    }
    /* 时钟上升边沿更新寄存器latch */
    core->decode = core->next_decode;
    core->execute = core->next_execute;
    core->memory = core->next_memory;
    core->commit = core->next_commit;
    return core->halted || drained(core);
}

int in_core_commit(INCore *core)
{
    if (!core->commit.has_data)
        return 0;
    trace_stage(core, "WB", core->commit);
    InsnLatch *l = &core->commit.latch;
    if (l->exception)
    {
        core->simcpu->trapped = core->halted = 1;
        core->simcpu->trap_cause = (uint32_t)(l->exception - 1);
        core->simcpu->trap_pc = l->pc;
        core->simcpu->trap_value = l->tval;
        return 1;
    }
    /* store 写入延迟到 WB，保证异常精确且不写入错误路径数据。 */
    if ((l->insn & OPCODE_MASK) == OPCODE_STORE)
    {
        unsigned width = 1u << ((l->insn >> 12) & 3); /* 根据 funct3 低两位计算写入字节数：SB=1、SH=2、SW=4。 */
        size_t address = l->result;
        for (unsigned j = 0; j < width; ++j)
            core->data[address + j] = (uint8_t)(l->rs2_value >> (j * 8));
    }
    if (l->writes_rd && l->rd)
        core->simcpu->regs[l->rd] = l->result;
    core->simcpu->regs[0] = 0;
    core->retired++;
    return 0;
}

void in_core_memory(INCore *core)
{
    if (!core->memory.has_data)
        return;
    trace_stage(core, "MEM", core->memory);
    core->next_commit = core->memory;
    InsnLatch *l = &core->next_commit.latch;
    /* opcode 为 insn[6:0]；funct3 为 insn[14:12]，用于细分指令类型。 */
    unsigned op = l->insn & OPCODE_MASK, f3 = (l->insn >> 12) & 7;
    if (l->exception || (op != OPCODE_LOAD && op != OPCODE_STORE))
        return;
    unsigned width = 1u << (f3 & 3); /* 低两位决定字节数：1、2、4；load 的 bit 2 区分无符号类型。 */
    uint32_t address = l->result;
    if (address & (width - 1)) /* 地址未对齐 */
        l->exception = op == OPCODE_LOAD ? 5 : 7;
    else if (!core->data || (uint64_t)address + width > core->data_size ||
             (uint64_t)address + width > UINT64_C(0x100000000)) /* 地址越界 */
        l->exception = op == OPCODE_LOAD ? 6 : 8;
    if (l->exception)
    {
        l->tval = address;
        return;
    }
    if (op == OPCODE_LOAD) /* load 在本阶段读取*/
    {
        uint32_t value = 0;
        for (unsigned j = 0; j < width; ++j)
            value |= (uint32_t)core->data[address + j] << (j * 8);
        /* 符号扩展 */
        if (f3 == 0)
            value = (value ^ 0x80u) - 0x80u;
        if (f3 == 1)
            value = (value ^ 0x8000u) - 0x8000u;
        l->result = value;
    }
}

void in_core_fetch(INCore *core)
{
    core->fetch = (CPUStage){0};
    if (core->fetch_done)
        return;
    uint32_t pc = core->fetch_pc;
    uint32_t offset = pc - core->program_base;
    /* 固定数组的末尾是演示环境的结束标记，不是一条 ISA 指令。 */
    if (!(pc & 3) && (uint64_t)offset == (uint64_t)core->program_size * 4)
    {
        core->fetch_done = 1;
        return;
    }
    core->fetch.has_data = 1;
    InsnLatch *l = &core->fetch.latch;
    l->pc = pc;
    if (pc & 3)
    {
        l->exception = 1;
        l->tval = pc;
    }
    else if (pc < core->program_base || offset / 4 >= core->program_size)
    {
        l->exception = 2;
        l->tval = pc;
    }
    else
    {
        core->fetch_index = offset / 4;
        l->insn = core->program[core->fetch_index++];
    }
    core->fetch_pc = pc + 4;
    trace_stage(core, "IF", core->fetch);
    core->next_decode = core->fetch;
    core->fetch = (CPUStage){0};
}
