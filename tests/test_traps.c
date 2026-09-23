/* 陷阱入口与 mret。钉住三件新语义：异常如何进入 mtvec 指向的处理程序、mstatus
 * 的状态位在进入与返回时如何翻转、以及各 CSR 的写掩码。
 * 「未安装处理程序时报告后停机」的旧行为由 test_rv32a / test_commit 覆盖，这里不重复。 */
#include "test_util.h"
#include "core/core.h"
#include "core/csr.h"
#include "core/cpu_state.h"

static INCore core;
static RISCVSIMCPUState cpu;
static unsigned cases;

/* 内嵌数组模式：程序就是这些指令字，取指窗口即数组本身。 */
static void run(const uint32_t *words, size_t count)
{
    in_core_init(&core, &cpu, &(CoreSetup){.program = words,
                                           .count = count,
                                           .base = BASE,
                                           .entry = BASE});
    in_core_run(&core, 200);
}

/* 陷阱进入与返回：ebreak 跳进处理程序，处理程序改 mepc 后 mret 回到主流程。 */
static void test_trap_entry_and_return(void)
{
    const uint32_t program[] = {
        0x800002b7,                          /* lui   x5, 0x80000，x5 = BASE */
        imm(0x13, 5, 0, 5, 20),              /* addi  x5, x5, 20，处理程序在 BASE+20 */
        csr(CSR_F3_RW, 0, 5, CSR_MTVEC),     /* csrrw x0, mtvec, x5 */
        0x00100073,                          /* ebreak，故障 PC = BASE+12 */
        jal(0, 24),                          /* jal   x0, +24，主流程跳过处理程序 */
        csr(CSR_F3_RS, 1, 0, CSR_MEPC),      /* x1 = 故障 PC */
        csr(CSR_F3_RS, 2, 0, CSR_MCAUSE),    /* x2 = 异常原因 */
        imm(0x13, 1, 0, 1, 4),               /* addi  x1, x1, 4，跳过 ebreak */
        csr(CSR_F3_RW, 0, 1, CSR_MEPC),      /* 返回地址改为 BASE+16 */
        0x30200073,                          /* mret */
        imm(0x13, 7, 0, 0, 7),               /* addi  x7, x0, 7，只有返回后才执行 */
    };
    run(program, sizeof(program) / sizeof(program[0]));

    CHECK(!cpu.trapped && in_core_finished(&core));
    CHECK(core.stats.retired == 10); /* ebreak 不退休；其余 10 条都退休 */
    CHECK(cpu.priv == PRIV_M);
    CHECK(cpu.csr[CSR_MTVEC] == BASE + 20);
    CHECK(cpu.csr[CSR_MEPC] == BASE + 16 && cpu.csr[CSR_MCAUSE] == 3);
    CHECK(cpu.csr[CSR_MTVAL] == BASE + 12);
    CHECK(cpu.regs[1] == BASE + 16 && cpu.regs[2] == 3);
    CHECK(cpu.regs[7] == 7);
    /* 进入陷阱时 MIE 为 0，所以 MPIE 记 0；mret 之后 MPIE 置 1、MPP 回到 U。 */
    CHECK(cpu.csr[CSR_MSTATUS] == MSTATUS_MPIE);
    cases++;
}

/* 状态位：进入陷阱时 MPP/MPIE/MIE 的翻转，以及 mret 的恢复。 */
static void test_status_stack(void)
{
    const uint32_t program[] = {
        csr(CSR_F3_RSI, 0, 8, CSR_MSTATUS),  /* csrrsi x0, mstatus, 8，打开 MIE */
        0x800002b7,                          /* lui   x5, 0x80000，x5 = BASE */
        imm(0x13, 5, 0, 5, 20),              /* addi  x5, x5, 20，处理程序在 BASE+20（不能与 ebreak 同址 */
        csr(CSR_F3_RW, 0, 5, CSR_MTVEC),     /* csrrw x0, mtvec, x5 */
        0x00100073,                          /* ebreak，故障 PC = BASE+16，入口另在 BASE+20，否则反复自陷 */
        csr(CSR_F3_RS, 3, 0, CSR_MSTATUS),   /* x3 = 进入陷阱后的 mstatus */
        csr(CSR_F3_RS, 4, 0, CSR_MEPC),      /* x4 = 故障 PC */
        imm(0x13, 4, 0, 4, 24),              /* x4 = BASE+40，落在 mret 之后 */
        csr(CSR_F3_RW, 0, 4, CSR_MEPC),
        0x30200073,                          /* mret，回到 BASE+40 */
        csr(CSR_F3_RS, 5, 0, CSR_MSTATUS),   /* x5 = 返回后的 mstatus */
        imm(0x13, 6, 0, 0, 6),               /* 收尾 */
    };
    run(program, sizeof(program) / sizeof(program[0]));

    CHECK(!cpu.trapped && in_core_finished(&core));
    /* MIE 被清、MPIE 记下原来的 1、MPP 记下陷入前的 M。 */
    CHECK(cpu.regs[3] == (MSTATUS_MPP | MSTATUS_MPIE));
    CHECK(cpu.regs[4] == BASE + 40);
    /* MIE 从 MPIE 恢复，MPIE 又置回 1。 */
    CHECK(cpu.regs[5] == (MSTATUS_MIE | MSTATUS_MPIE));
    CHECK(cpu.priv == PRIV_M);
    cases++;
}

/* 写掩码：未实现的位写不进去，读回来就是实际支持的字段（WARL）。 */
static void test_write_masks(void)
{
    const uint32_t program[] = {
        imm(0x13, 5, 0, 0, -1),              /* addi  x5, x0, -1，全 1 */
        csr(CSR_F3_RW, 0, 5, CSR_MSTATUS),
        csr(CSR_F3_RW, 0, 5, CSR_MTVEC),     /* MODE 两位被丢掉 */
        csr(CSR_F3_RW, 0, 5, CSR_MEPC),      /* 低两位被丢掉 */
        csr(CSR_F3_RS, 1, 0, CSR_MSTATUS),
        csr(CSR_F3_RS, 2, 0, CSR_MTVEC),
        csr(CSR_F3_RS, 3, 0, CSR_MEPC),
    };
    run(program, sizeof(program) / sizeof(program[0]));

    CHECK(!cpu.trapped && in_core_finished(&core));
    CHECK(cpu.regs[1] == MSTATUS_MASK);
    CHECK(cpu.regs[2] == ~MTVEC_MODE_MASK);
    CHECK(cpu.regs[3] == MEPC_MASK);
    cases++;
}

int main(void)
{
    test_trap_entry_and_return();
    test_status_stack();
    test_write_masks();
    printf("TRAPS: %u directed cases passed\n", cases);
    return 0;
}
