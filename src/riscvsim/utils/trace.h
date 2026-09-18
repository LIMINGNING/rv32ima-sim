#ifndef RISCVSIM_TRACE_H
#define RISCVSIM_TRACE_H

#include "../core/core.h"

/* 根据 trace 开关输出阶段状态，不修改核心数据。 */
void trace_stage(const INCore *core, const char *name, CPUStage stage);

void trace_commit(const INCore *core, int trap);
#endif
