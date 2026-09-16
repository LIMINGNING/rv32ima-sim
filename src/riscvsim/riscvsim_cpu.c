#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "riscvsim_cpu.h"
#include "core/core.h"

int main(void)
{
    /* RAM[16] 初始为 10：AMO 加 3，再由 LR/SC 加 1，最终为 14。 */
    static const uint32_t program[] = {
        0x01000093u, /* addi x1, x0, 16 */
        0x00300113u, /* addi x2, x0, 3 */
        0x0620a1afu, /* amoadd.w.aqrl x3, x2, (x1) */
        0x1400a22fu, /* lr.w.aq x4, (x1) */
        0x00120213u, /* addi x4, x4, 1 */
        0x1a40a2afu, /* sc.w.rl x5, x4, (x1) */
        0x0000a303u, /* lw x6, 0(x1) */
    };
    /* 创建cpu及core */
    RISCVSIMCPUState cpu = {0};
    INCore core = {0};
    uint8_t ram[1024] = {0};
    size_t count = sizeof(program) / sizeof(program[0]);
    in_core_init(&core, &cpu, program, count,
                 0x80000000u, ram, sizeof(ram));
    ram[16] = 10;
    core.trace = 1;

    in_core_run(&core, 100);

    assert(cpu.regs[3] == 10 && cpu.regs[4] == 14 && cpu.regs[5] == 0);
    assert(cpu.regs[6] == 14 && ram[16] == 14);
    assert(cpu.clock == 20 && core.memory_stalls == 3 && core.stalls == 6);
    assert(!cpu.trapped);
    assert(core.retired == core.program_size);
    assert(!core.decode.has_data && !core.execute.has_data &&
           !core.memory.has_data && !core.commit.has_data);
    printf("clock = %" PRIu64 ", retired = %" PRIu64 "\n",
           cpu.clock, core.retired);
    return 0;
}
