#include "core.h"
#include "../riscvsim_cpu.h"
#include "../utils/trace.h"

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
        in_core_invalidate_reservation(core, (uint32_t)address, width); /* 通知缓存一致性，使该地址内存标记失效 */
    }
    if (l->writes_rd && l->rd)
        core->simcpu->regs[l->rd] = l->result;
    core->simcpu->regs[0] = 0;
    core->retired++;
    return 0;
}
