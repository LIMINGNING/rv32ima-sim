#ifndef RISCVSIM_CORE_H
#define RISCVSIM_CORE_H

#include "cpu_latches.h"
#include "../riscvsim_macros.h"
#include <stdint.h>
#include <stddef.h>

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
    /* 从地址 0 开始的小端数据 RAM；访存地址直接作为数组下标。 */
    uint8_t *data;
    size_t data_size;

    struct RISCVSIMCPUState *simcpu; // 回指父对象
} INCore;

void in_core_init(INCore *core, struct RISCVSIMCPUState *cpu,
                  const uint32_t *program, size_t count, uint32_t base,
                  uint8_t *data, size_t data_size);

/* Run at most cycles ticks; a stop request still counts the current tick. */
void in_core_run(INCore *core, uint64_t cycles);
int in_core_run_5_stage(INCore *core);

int in_core_commit(INCore *core);
void in_core_memory(INCore *core);
void in_core_execute(INCore *core);
void in_core_decode(INCore *core);
void in_core_fetch(INCore *core);

#endif
