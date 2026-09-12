#include "trace.h"
#include "../riscvsim_cpu.h"
#include <inttypes.h>
#include <stdio.h>

void trace_stage(const INCore *core, const char *name, CPUStage stage)
{
    if (!core->trace || !stage.has_data) return;
    printf("cycle=%" PRIu64 " %-3s pc=0x%08" PRIx32 " insn=0x%08" PRIx32 "\n",
           core->simcpu->clock + 1, name, stage.latch.pc, stage.latch.insn);
}
