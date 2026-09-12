#include "core/core.h"
#include "riscvsim_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define BASE UINT32_C(0x80000000)
static INCore core;
static RISCVSIMCPUState cpu;
static uint8_t ram[64];
static unsigned cases;
static uint32_t imm(unsigned op, unsigned rd, unsigned f, unsigned rs, int value)
{
    return ((uint32_t)value & 4095) << 20 | rs << 15 | f << 12 | rd << 7 | op;
}
static uint32_t reg(unsigned f, unsigned f7)
{
    return f7 << 25 | 2u << 20 | 1u << 15 | f << 12 | 3u << 7 | 0x33;
}
static uint32_t store(unsigned f, unsigned rs, unsigned base, int offset)
{
    uint32_t v = (uint32_t)offset & 4095;
    return (v >> 5) << 25 | rs << 20 | base << 15 | f << 12 | (v & 31) << 7 | 0x23;
}
static uint32_t branch(unsigned f, int offset)
{
    uint32_t v = (uint32_t)offset & 8191;
    return (v >> 12) << 31 | ((v >> 5) & 63) << 25 | 2u << 20 | 1u << 15 |
           f << 12 | ((v >> 1) & 15) << 8 | ((v >> 11) & 1) << 7 | 0x63;
}
static uint32_t jal(unsigned rd, int offset)
{
    uint32_t v = (uint32_t)offset & 0x1fffff;
    return (v >> 20) << 31 | ((v >> 1) & 1023) << 21 |
           ((v >> 11) & 1) << 20 | ((v >> 12) & 255) << 12 | rd << 7 | 0x6f;
}
static void setup(const uint32_t *p, size_t n)
{
    memset(ram, 0, sizeof(ram));
    in_core_init(&core, &cpu, p, n, BASE, ram, sizeof(ram));
    cases++;
}
static void run(void) { in_core_run(&core, 500); }
static void alu(uint32_t insn, uint32_t a, uint32_t b, uint32_t expected)
{
    setup(&insn, 1);
    cpu.regs[1] = a; cpu.regs[2] = b;
    run();
    CHECK(!cpu.trapped && core.retired == 1 && cpu.clock == 5);
    CHECK(cpu.regs[3] == expected && cpu.regs[0] == 0);
}
static void test_alu(void)
{
    alu(reg(0,0), 0xffffffff, 1, 0);
    alu(reg(0,32), 0x80000000, 1, 0x7fffffff);
    alu(reg(1,0), 1, 33, 2);
    alu(reg(2,0), 0x80000000, 1, 1);
    alu(reg(3,0), 0x80000000, 1, 0);
    alu(reg(4,0), 0xaa, 0x55, 0xff);
    alu(reg(5,0), 0x80000000, 31, 1);
    alu(reg(5,32), 0x80000000, 31, 0xffffffff);
    alu(reg(5,32), 0x80000000, 32, 0x80000000);
    alu(reg(6,0), 0xa0, 5, 0xa5);
    alu(reg(7,0), 0xaa, 0xf, 0xa);
    alu(imm(0x13,3,0,1,-2048), 2047, 0, 0xffffffff);
    alu(imm(0x13,3,2,1,-1), 0x80000000, 0, 1);
    alu(imm(0x13,3,3,1,-1), 1, 0, 1);
    alu(imm(0x13,3,4,1,-1), 0x55, 0, 0xffffffaa);
    alu(imm(0x13,3,6,1,5), 0xa0, 0, 0xa5);
    alu(imm(0x13,3,7,1,15), 0xaa, 0, 0xa);
    alu(imm(0x13,3,1,1,31), 1, 0, 0x80000000);
    alu(imm(0x13,3,5,1,31), 0x80000000, 0, 1);
    alu(imm(0x13,3,5,1,0x41f), 0x80000000, 0, 0xffffffff);
    alu(0xabcde1b7, 0, 0, 0xabcde000); /* LUI */
    alu(0x00001197, 0, 0, BASE + 4096); /* AUIPC */
}
static void test_memory(void)
{
    const unsigned fs[] = {0,1,2,4,5};
    const uint32_t expected[] = {0xffffff80,0xffff8180,0x83828180,0x80,0x8180};
    for (unsigned j = 0; j < 5; ++j) {
        uint32_t p = imm(3,3,fs[j],1,-4);
        setup(&p,1); cpu.regs[1] = 8;
        ram[4]=0x80; ram[5]=0x81; ram[6]=0x82; ram[7]=0x83;
        run(); CHECK(!cpu.trapped && cpu.regs[3] == expected[j]);
    }
    for (unsigned f = 0; f < 3; ++f) {
        uint32_t p[] = {store(f,2,1,-4), imm(3,3,f == 2 ? 2 : f+4,1,-4)};
        setup(p,2); cpu.regs[1]=8; cpu.regs[2]=0x12345678;
        run(); CHECK(!cpu.trapped);
        uint32_t mask = f == 2 ? 0xffffffff : (1u << ((1u << f)*8))-1;
        CHECK(cpu.regs[3] == (0x12345678 & mask));
        CHECK(ram[3] == 0 && ram[4+(1u<<f)] == 0);
    }
}
static void test_hazards(void)
{
    uint32_t p[] = {imm(0x13,1,0,0,7), imm(0x13,1,0,1,1),
                    store(2,1,0,0), imm(3,2,2,0,0), reg(0,0)};
    setup(p,5); run();
    CHECK(!cpu.trapped && cpu.regs[1]==8 && cpu.regs[2]==8 && cpu.regs[3]==16);
    CHECK(core.stalls == 6 && cpu.clock == 15 && core.retired == 5);
    uint32_t z[] = {imm(0x13,0,0,0,5), imm(0x13,1,0,0,3)};
    setup(z,2); run(); CHECK(cpu.regs[0]==0 && cpu.regs[1]==3 && core.stalls==0);
    uint32_t independent[] = {imm(0x13,1,0,0,1),imm(0x13,2,0,0,2),imm(0x13,3,0,0,3)};
    setup(independent,3); in_core_run(&core,2); CHECK(cpu.clock==2);
    run(); CHECK(cpu.clock==7 && core.retired==3);
    run(); CHECK(cpu.clock==7);
    setup(NULL,0); run(); CHECK(cpu.clock==0);
}
static void test_control(void)
{
    const unsigned f[] = {0,1,4,5,6,7};
    for (unsigned j=0;j<6;++j) for (unsigned taken=0;taken<2;++taken) {
        uint32_t p[] = {branch(f[j],8), imm(0x13,3,0,0,9), imm(0x13,4,0,0,7)};
        setup(p,3);
        if (f[j]==0) { cpu.regs[1]=1; cpu.regs[2]=taken ? 1 : 2; }
        if (f[j]==1) { cpu.regs[1]=1; cpu.regs[2]=taken ? 2 : 1; }
        if (f[j]==4) { cpu.regs[1]=taken ? 0xffffffff : 2; cpu.regs[2]=1; }
        if (f[j]==5) { cpu.regs[1]=taken ? 1 : 0xffffffff; cpu.regs[2]=1; }
        if (f[j]==6) { cpu.regs[1]=taken ? 1 : 0xffffffff; cpu.regs[2]=2; }
        if (f[j]==7) { cpu.regs[1]=taken ? 0xffffffff : 1; cpu.regs[2]=2; }
        run(); CHECK(!cpu.trapped && cpu.regs[4]==7);
        CHECK(cpu.regs[3]==(taken?0u:9u) && core.flushes==taken);
        CHECK(core.retired==(taken?2u:3u));
    }
    uint32_t p[] = {jal(1,8), 0xffffffff, imm(0x13,3,0,1,0)};
    setup(p,3); run(); CHECK(!cpu.trapped && cpu.regs[3]==BASE+4 && core.retired==2);
    uint32_t q[] = {imm(0x67,1,0,1,0),store(2,2,0,0),imm(0x13,3,0,1,0)};
    setup(q,3); cpu.regs[1]=BASE+9; cpu.regs[2]=42; run();
    CHECK(!cpu.trapped && cpu.regs[3]==BASE+4 && ram[0]==0);
    uint32_t loop[] = {imm(0x13,1,0,1,-1),branch(1,-4)};
    setup(loop,2); cpu.regs[1]=3; run();
    CHECK(!cpu.trapped && cpu.regs[1]==0 && core.retired==6 && core.flushes==2);
}
static void fault(uint32_t instruction, uint32_t cause, uint32_t value)
{
    uint32_t p[] = {imm(0x13,5,0,0,7), instruction, store(2,5,0,0),imm(0x13,6,0,0,9)};
    setup(p,4); run();
    CHECK(cpu.trapped && cpu.trap_cause==cause && cpu.trap_pc==BASE+4);
    CHECK(cpu.trap_value==value && core.retired==1 && cpu.regs[5]==7);
    CHECK(ram[0]==0 && cpu.regs[6]==0);
}
static void test_faults(void)
{
    fault(0xffffffff,2,0xffffffff);
    fault(reg(0,1),2,reg(0,1)); /* M 不属于 RV32I */
    fault(imm(0x13,3,1,0,32),2,imm(0x13,3,1,0,32));
    fault(0x0000100f,2,0x0000100f); /* FENCE.I 是独立扩展 */
    fault(0x00000073,11,0);
    fault(0x00100073,3,BASE+4);
    fault(imm(3,0,2,0,1),4,1); /* 即使 rd=x0，load 仍触发异常 */
    fault(imm(3,3,2,0,64),5,64);
    fault(store(1,5,0,1),6,1);
    fault(store(2,5,0,64),7,64);
    fault(jal(3,2),0,BASE+6);
    uint32_t p[]={branch(1,2)}; setup(p,1); run(); CHECK(!cpu.trapped);
    uint32_t q[]={jal(0,12)}; setup(q,1); run();
    CHECK(cpu.trapped && cpu.trap_cause==1 && cpu.trap_pc==BASE+12 && core.retired==1);
    uint32_t fence=0x0ff0000f; setup(&fence,1); run(); CHECK(!cpu.trapped && core.retired==1);
}
int main(void)
{
    test_alu(); test_memory(); test_hazards(); test_control(); test_faults();
    printf("RV32I: %u directed cases passed\n", cases);
    return 0;
}
