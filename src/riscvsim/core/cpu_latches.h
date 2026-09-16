#ifndef RISCVSIM_CPU_LATCHES_H
#define RISCVSIM_CPU_LATCHES_H

#include <stdint.h>

/* 阻塞式原子访存单元：每次 MEM 调用只推进一个状态。 */
typedef enum AtomicPhase
{
    ATOMIC_READ = 0,
    ATOMIC_MODIFY,
    ATOMIC_WRITE
} AtomicPhase;

/* 随指令在流水阶段间传递的数据，各阶段通过结构体赋值保存独立副本。 */
typedef struct InsnLatch
{
    uint32_t pc;        /* IF：当前这条指令的地址。 */
    uint32_t insn;      /* IF：取出的 32 位指令机器码。 */
    uint32_t rs1_value; /* ID：读取的源寄存器 rs1 的值，供 EX 使用。 */
    uint32_t rs2_value; /* ID：读取的 rs2 的值，用作运算/比较操作数或 store 数据。 */
    uint32_t imm;       /* ID：重组后的立即数；需要时符号扩展为 32 位。 */
    uint32_t result;    /* EX：运算结果或普通访存地址；MEM：load/LR/AMO 读取值或 SC 状态；WB：写回 rd。 */
    uint32_t mem_addr;  /* EX：保存 rs1 给出的原子访存地址，供 MEM 使用。 */
    uint32_t mem_value; /* MEM：AMO 运算生成的新值或 SC 从 rs2 取得的待写数据。 */
    AtomicPhase atomic_phase; /* MEM：逐拍更新原子操作状态；IF 清零时初始为 ATOMIC_READ。 */
    int m_result_ready; /* EX：M 指令首次计算结果后置 1，等待期间避免重复计算。 */
    unsigned ex_cycles_left; /* EX：首次设置 M 指令延迟，随后每次推进递减，完成时清零。 */
    unsigned rd;       /* ID：目的寄存器编号，范围 0～31。 */
    unsigned rs1;      /* ID：第一个源寄存器编号，仅在指令使用 rs1 时有意义。 */
    unsigned rs2;      /* ID：第二个源寄存器编号，仅在指令使用 rs2 时有意义。 */
    int writes_rd;     /* ID：确定指令是否写 rd；WB 根据此标志写回，忽略 x0。 */
    int exception;     /* IF/ID/EX/MEM：检测异常时设置 cause + 1，0 表示无异常；WB 报告。 */
    uint32_t tval;     /* IF/ID/EX/MEM：随异常记录故障地址或非法机器码等；WB 保存，无附加信息时为 0。 */
} InsnLatch;

/* 一个流水阶段的状态：记录是否有有效指令，并保存该指令的数据。 */
typedef struct CPUStage
{
    int has_data;        /* 非零表示有有效指令；0 表示空闲或气泡，不产生副作用。 */
    InsnLatch latch; /* has_data 非零时有效，本阶段直接保存指令数据 */
} CPUStage;

#endif
