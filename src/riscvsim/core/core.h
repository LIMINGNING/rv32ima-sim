#ifndef RISCVSIM_CORE_H
#define RISCVSIM_CORE_H

#include "cpu_latches.h"
#include "../riscvsim_macros.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Physical bus: probe MUST NOT cause device side effects. Return 0 on success.
 * access: 0=read, 1=write, 2=instruction fetch. No virtual translation yet. */
typedef struct SimBus {
    void *opaque;
    int (*probe)(void *, uint32_t, unsigned, int);
    int (*read)(void *, uint32_t, unsigned, uint32_t *);
    int (*write)(void *, uint32_t, unsigned, uint32_t);
} SimBus;

typedef struct INCore
{
    /* 流水线级 */
    CPUStage fetch;
    CPUStage decode;
    CPUStage execute;
    CPUStage memory;
    CPUStage commit;

    /* 本拍计算，下个时钟边沿生效。 */
    CPUStage next_decode, next_execute, next_memory, next_commit;

    const uint32_t *program;
    size_t program_size; /* 指令条数 */
    size_t fetch_index;
    uint32_t program_base;
    uint64_t retired;

    uint32_t fetch_pc;
    int fetch_done, stalled, redirected, halted, trace;
    uint64_t stalls, flushes;
    int memory_stalled;    /* 本拍 MEM 尚未完成，反压 EX/ID/IF。 */
    uint64_t memory_stalls; /* 与寄存器数据冒险 stalls 分开统计。 */
    unsigned mul_cycles, div_cycles; /* EX 总延迟，在运行前配置。 */
    int execute_stalled;
    uint64_t execute_stalls; /* 乘除单元等待拍数，不含 MEM 反压。 */
    /* 从地址 0 开始的小端数据 RAM；访存地址直接作为数组下标。 */
    uint8_t *data;
    size_t data_size;

    SimBus bus; /* Optional mapped memory; legacy array mode remains supported. */
    FILE *commit_trace;
    uint32_t stop_pc;
    int stop_pc_valid;

    struct RISCVSIMCPUState *simcpu; // 回指父对象
} INCore;

void in_core_init(INCore *core, struct RISCVSIMCPUState *cpu,
                  const uint32_t *program, size_t count, uint32_t base,
                  uint8_t *data, size_t data_size);

/* Run at most cycles ticks; a stop request still counts the current tick. */
void in_core_run(INCore *core, uint64_t cycles);
/* 只允许在初始化后、运行前配置非零 EX 延迟；成功返回 1。 */
int in_core_set_m_latency(INCore *core, unsigned mul_cycles, unsigned div_cycles);
int in_core_run_5_stage(INCore *core);

int in_core_commit(INCore *core);
void in_core_memory(INCore *core);
void in_core_atomic_memory(INCore *core);
/* 数据写入使重叠的 LR 保留失效；普通 store 和 AMO 共用。 */
void in_core_invalidate_reservation(INCore *core, uint32_t address, unsigned width);
void in_core_execute(INCore *core);
void in_core_decode(INCore *core);
void in_core_fetch(INCore *core);

#endif
