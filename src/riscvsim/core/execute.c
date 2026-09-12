#include "core.h"
#include "../utils/trace.h"

/* 转换符号位后做无符号比较，避免宿主机有符号溢出。 */
static int signed_less(uint32_t a, uint32_t b)
{
    return (a ^ 0x80000000u) < (b ^ 0x80000000u);
}

static uint32_t sra(uint32_t a, unsigned shift)
{
    shift &= 31; /* RV32 移位量只取低 5 位，范围 0～31。 */
    if (!shift) return a;
    return (a >> shift) | ((a & 0x80000000u) ? (~UINT32_C(0) << (32 - shift)) : 0);
}

void in_core_execute(INCore *core)
{
    if (!core->execute.has_data) return;
    trace_stage(core, "EX", core->execute);
    core->next_memory = core->execute;
    InsnLatch *l = &core->next_memory.latch;
    if (l->exception) return;
    /* opcode 为 insn[6:0]；funct3 为 insn[14:12]，用于细分指令类型。 */
    unsigned op = l->insn & OPCODE_MASK, f3 = (l->insn >> 12) & 7;
    uint32_t a = l->rs1_value, b = l->rs2_value, target = 0;
    int jump = 0;
    switch (op) {
    case OPCODE_LUI: l->result = l->imm; break;
    case OPCODE_AUIPC: l->result = l->pc + l->imm; break;
    case OPCODE_LOAD: case OPCODE_STORE: l->result = a + l->imm; break;
    case OPCODE_JAL: case OPCODE_JALR:
        l->result = l->pc + 4;
        /* JAL 使用 PC 相对地址；JALR 使用寄存器加偏移，并清除地址最低位。 */
        target = op == OPCODE_JAL ? l->pc + l->imm : (a + l->imm) & ~1u;
        jump = 1;
        break;
    case OPCODE_BRANCH:
        switch (f3) {
        case 0: jump = a == b; break;
        case 1: jump = a != b; break;
        case 4: jump = signed_less(a, b); break;
        case 5: jump = !signed_less(a, b); break;
        case 6: jump = a < b; break;
        case 7: jump = a >= b; break;
        }
        target = l->pc + l->imm;
        break;
    case OPCODE_OP_IMM: case OPCODE_OP:
        if (op == OPCODE_OP_IMM) b = l->imm;
        /* 合法编码已由 ID 检查；bit 30 区分 ADD/SUB、逻辑/算术右移。
         * 移位时 b & 31 提取低 5 位移位量。 */
        switch (f3) {
        case 0: l->result = op == OPCODE_OP && (l->insn >> 30 & 1) ? a - b : a + b; break;
        case 1: l->result = a << (b & 31); break;
        case 2: l->result = (uint32_t)signed_less(a, b); break;
        case 3: l->result = a < b; break;
        case 4: l->result = a ^ b; break;
        case 5: l->result = (l->insn >> 30 & 1) ? sra(a, b) : a >> (b & 31); break;
        case 6: l->result = a | b; break;
        case 7: l->result = a & b; break;
        }
        break;
    }
    if (jump) {
        if (target & 3) { l->exception = 1; l->tval = target; }
        else {
            core->fetch_pc = target;
            core->fetch_done = 0;
            core->redirected = 1;
            core->flushes++;
        }
    }
}
