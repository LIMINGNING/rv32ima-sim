#include "core_internal.h"
#include "csr.h"
#include "cpu_state.h"
#include "../utils/trace.h"

/* 异常在 WB 统一生效：较老指令已提交，年轻指令作废。装了处理程序就跳进 mtvec，
 * 否则保留原有的裸机调试行为——记录 cause/pc/tval 后停机：地址 0 在任何受支持的
 * 内存布局里都取不到指令，跳过去只会立刻再故障一次。
 * 返回值恒为 1：调用方本拍不再推进前端，年轻的 IF/ID/EX/MEM 随时钟边沿一起清空。 */
static int enter_trap(INCore *core, InsnLatch *l)
{
    RISCVSIMCPUState *cpu = core->simcpu;
    uint32_t cause = (uint32_t)(l->exception - 1);
    uint32_t status = cpu->csr[CSR_MSTATUS];

    cpu->trap_cause = cause;
    cpu->trap_pc = l->pc;
    cpu->trap_value = l->tval;
    trace_commit(core, 1);

    if (!cpu->csr[CSR_MTVEC])
    {
        cpu->trapped = core->halted = 1;
        return 1;
    }

    /* MPP 保存陷入前的特权级，MPIE 保存 MIE，随后关中断；mret 反向恢复。 */
    cpu->csr[CSR_MSTATUS] = (status & ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_MIE)) |
                            ((cpu->priv << MSTATUS_MPP_SHIFT) & MSTATUS_MPP) |
                            (status & MSTATUS_MIE ? MSTATUS_MPIE : 0);
    cpu->csr[CSR_MEPC] = l->pc & MEPC_MASK;
    cpu->csr[CSR_MCAUSE] = cause;
    cpu->csr[CSR_MTVAL] = l->tval;
    cpu->priv = PRIV_M;

    core->fetch_pc = cpu->csr[CSR_MTVEC] & ~MTVEC_MODE_MASK;
    core->fetch_done = 0;
    return 1;
}

int in_core_commit(INCore *core)
{
    if (!core->commit.has_data)
        return 0;
    trace_stage(core, "WB", core->commit);
    InsnLatch *l = &core->commit.latch;
    if (l->exception)
        return enter_trap(core, l);
    unsigned op = l->insn & OPCODE_MASK;
    uint32_t target = 0; /* MRET 的返回地址；redirect 置位时本拍重定向前端。 */
    int redirect = 0;
    /* Bus reads (including MMIO) happen only at retirement. A younger load
     * must never consume a device value ahead of an older faulting access. */
    if (op == OPCODE_LOAD && core->bus.read) {
        unsigned f3 = (l->insn >> 12) & 7, width = 1u << (f3 & 3);
        uint32_t value;
        if (core->bus.read(core->bus.opaque, l->mem_addr, width, &value)) {
            l->exception = 6; l->tval = l->mem_addr;
            return enter_trap(core, l);
        }
        if (f3 == 0) value = (value ^ 0x80u) - 0x80u;
        if (f3 == 1) value = (value ^ 0x8000u) - 0x8000u;
        l->result = value;
    }
    /* store 写入延迟到 WB，保证异常精确且不写入错误路径数据。 */
    if (op == OPCODE_STORE)
    {
        unsigned width = 1u << ((l->insn >> 12) & 3); /* 根据 funct3 低两位计算写入字节数：SB=1、SH=2、SW=4。 */
        size_t address = l->result;
        if (core->bus.write) {
            if (core->bus.write(core->bus.opaque, (uint32_t)address, width, l->rs2_value)) {
                l->exception = 8; l->tval = (uint32_t)address;
                return enter_trap(core, l);
            }
        } else {
            for (unsigned j = 0; j < width; ++j)
                core->data[address + j] = (uint8_t)(l->rs2_value >> (j * 8));
            in_core_invalidate_reservation(core, (uint32_t)address, width);
        }
    }
    /* MRET：陷阱的镜像——状态恢复与 PC 重定向都在 WB 一起生效。特权级要等到 WB
     * 才翻转，取指才会在正确的特权级下重新开始；本拍前端同样不再推进。 */
    if (l->insn == INSN_MRET)
    {
        RISCVSIMCPUState *cpu = core->simcpu;
        if (cpu->priv != PRIV_M)
        {
            l->exception = 3; /* illegal instruction, cause 2 */
            l->tval = l->insn;
            return enter_trap(core, l);
        }
        uint32_t status = cpu->csr[CSR_MSTATUS];
        cpu->priv = (status & MSTATUS_MPP) >> MSTATUS_MPP_SHIFT;
        cpu->csr[CSR_MSTATUS] = (status & ~(MSTATUS_MPP | MSTATUS_MPIE | MSTATUS_MIE)) |
                                (status & MSTATUS_MPIE ? MSTATUS_MIE : 0) |
                                MSTATUS_MPIE; /* MPP 复位到最低特权级 U */
        target = cpu->csr[CSR_MEPC] & MEPC_MASK;
        redirect = 1;
    }
    /* CSR 读改写延迟到 WB：与 store 一致，保证异常精确。 */
    if (op == OPCODE_SYSTEM)
    {
        unsigned f3 = (l->insn >> 12) & 7;
        if (f3 != 0)
        {
            uint32_t src = f3 > 3 ? l->rs1 : l->rs1_value; /* 立即数形式取 zimm */
            uint32_t old_value = 0;
            if (csr_access(core->simcpu, l->csr_addr, f3, l->rs1, src, &old_value))
            {
                l->exception = 3; /* illegal instruction, cause 2 */
                l->tval = l->insn;
                return enter_trap(core, l);
            }
            l->result = old_value; /* rd 写回 CSR 旧值 */
        }
    }
    if (l->writes_rd && l->rd)
        core->simcpu->regs[l->rd] = l->result;
    core->simcpu->regs[0] = 0;
    core->stats.retired++;
    core->simcpu->instret++;
    trace_commit(core, 0);
    if (core->config.stop_pc_valid && l->pc == core->config.stop_pc) {
        core->halted = 1;
        return 1;
    }
    if (redirect) {
        core->fetch_pc = target;
        core->fetch_done = 0;
        return 1;
    }
    return 0;
}
