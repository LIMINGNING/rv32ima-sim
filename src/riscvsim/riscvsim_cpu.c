#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "riscvsim_cpu.h"
#include "core/core.h"

int main(void)
{
    /* 无数据相关的 RV32I 演示程序。 */
    static const uint32_t program[] = {
        0x00000013u, /* nop */
        0x00100093u, /* addi x1, x0, 1 */
        0x00200113u, /* addi x2, x0, 2 */
        0x00300193u, /* addi x3, x0, 3 */
        0x00000013u, /* nop */
        0x00000013u,
    };
    /* 创建cpu及core */
    RISCVSIMCPUState cpu = {0};
    INCore core = {0};
    uint8_t ram[1024] = {0};
    in_core_init(&core, &cpu, program, sizeof(program) / sizeof(program[0]),
                 0x80000000u, ram, sizeof(ram));
    core.trace = 1;

    in_core_run(&core, 100);

    assert(cpu.regs[1] == 1 && cpu.regs[2] == 2 && cpu.regs[3] == 3);
    assert(!cpu.trapped);
    assert(core.retired == core.program_size);
    assert(cpu.clock == core.program_size + 4);
    assert(!core.decode.has_data && !core.execute.has_data &&
           !core.memory.has_data && !core.commit.has_data);
    printf("clock = %" PRIu64 ", retired = %" PRIu64 "\n",
           cpu.clock, core.retired);
    return 0;
}
