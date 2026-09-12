#ifndef RISCVSIM_CPU_LATCHES_H
#define RISCVSIM_CPU_LATCHES_H

#include <stdint.h>

/* 随指令在流水阶段间传递的数据，各阶段通过结构体赋值保存独立副本。 */
typedef struct InsnLatch
{
    uint32_t pc;        /* IF：当前这条指令的地址。 */
    uint32_t insn;      /* IF：取出的 32 位指令机器码。 */
    uint32_t rs1_value; /* ID：读取的源寄存器 rs1 的值，供 EX 使用。 */
    uint32_t rs2_value; /* ID：读取的 rs2 的值，用作运算/比较操作数或 store 数据。 */
    uint32_t imm;       /* ID：重组后的立即数；需要时符号扩展为 32 位。 */
    uint32_t result;    /* EX：运算结果或访存地址；load 在 MEM 将其替换为加载值。 */
    unsigned rd;       /* ID：目的寄存器编号，范围 0～31。 */
    unsigned rs1;      /* ID：第一个源寄存器编号，仅在指令使用 rs1 时有意义。 */
    unsigned rs2;      /* ID：第二个源寄存器编号，仅在指令使用 rs2 时有意义。 */
    int writes_rd;     /* 指令是否需要写回 rd；WB 仍会忽略对 x0 的写入。 */
    int exception;     /* 0 表示无异常，否则为 RISC-V cause + 1；到 WB 报告。 */
    uint32_t tval;     /* 异常附加信息：如故障地址或非法机器码，无附加信息时为 0。 */
} InsnLatch;

/* 一个流水阶段的状态：记录是否有有效指令，并保存该指令的数据。 */
typedef struct CPUStage
{
    int has_data;        /* 非零表示有有效指令；0 表示空闲或气泡，不产生副作用。 */
    int stage_exec_done; /* 预留：标记本阶段处理已完成；当前单拍实现尚未使用。 */
    InsnLatch latch; /* has_data 非零时有效，本阶段直接保存指令数据 */
} CPUStage;

#endif
