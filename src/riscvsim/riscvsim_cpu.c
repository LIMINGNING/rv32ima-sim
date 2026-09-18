#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "riscvsim_cpu.h"
#include "core/core.h"
#include "system/memory.h"

static int demo(void)
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

static int number(const char *s, uint64_t *out)
{
    char *end;
    if (!*s || *s == '-') return -1;
    errno = 0;
    unsigned long long n = strtoull(s, &end, 0);
    if (errno || *end) return -1;
    *out = n;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 1) return demo();
    const char *bin = NULL, *trace = NULL, *backend = "flat";
    uint64_t base = UINT32_C(0x80000000), ram_size = 1024 * 1024;
    uint64_t cycles = 100000, stop = 0;
    int has_stop = 0, stages = 0;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help")) {
            puts("sim --bin FILE [--trace-json FILE] [--backend flat|marss]\n"
                 "    [--base ADDRESS] [--ram-size BYTES] [--max-cycles N]\n"
                 "    [--stop-pc ADDRESS] [--stage-trace]\n"
                 "Raw little-endian RV32IM image; entry equals base.\n"
                 "Image end drains the pipeline; stop-pc stops AFTER retirement.\n"
                 "Exit: 0 completed, 1 trap, 2 configuration/I/O error, 3 timeout.");
            return 0;
        }
        if (!strcmp(arg, "--stage-trace")) { stages = 1; continue; }
        if (++i == argc) { fprintf(stderr, "missing value for %s\n", arg); return 2; }
        const char *value = argv[i];
        uint64_t *dest = NULL;
        if (!strcmp(arg, "--bin")) bin = value;
        else if (!strcmp(arg, "--trace-json")) trace = value;
        else if (!strcmp(arg, "--backend")) backend = value;
        else if (!strcmp(arg, "--base")) dest = &base;
        else if (!strcmp(arg, "--ram-size")) dest = &ram_size;
        else if (!strcmp(arg, "--max-cycles")) dest = &cycles;
        else if (!strcmp(arg, "--stop-pc")) { dest = &stop; has_stop = 1; }
        else { fprintf(stderr, "unknown option: %s\n", arg); return 2; }
        if (dest && number(value, dest)) { fprintf(stderr, "invalid number: %s\n", value); return 2; }
    }
    if (!bin || base > UINT32_MAX || stop > UINT32_MAX || !cycles ||
        !ram_size || ram_size > 256u * 1024 * 1024 ||
        (has_stop && (stop & 3)) ||
        (strcmp(backend, "flat") && strcmp(backend, "marss"))) {
        fputs("invalid configuration; use --help\n", stderr); return 2;
    }
    struct stat input_stat, output_stat;
    if (trace && (!strcmp(bin, trace) ||
        (!stat(bin, &input_stat) && !stat(trace, &output_stat) &&
         input_stat.st_dev == output_stat.st_dev && input_stat.st_ino == output_stat.st_ino))) {
        fputs("trace output must differ from input image\n", stderr); return 2;
    }
    FILE *input = fopen(bin, "rb");
    if (!input) { perror(bin); return 2; }
    if (fseek(input, 0, SEEK_END)) { fclose(input); return 2; }
    long length = ftell(input);
    if (length <= 0 || length % 4 || (uint64_t)length > ram_size) {
        fputs("image must be nonempty, a multiple of 4 bytes, and fit RAM\n", stderr);
        fclose(input); return 2;
    }
    rewind(input);
    SimMemory memory;
    if (sim_memory_init(&memory, (uint32_t)base, (size_t)ram_size, !strcmp(backend, "marss"))) {
        fputs("cannot initialize backend (check page alignment / MARSS build option)\n", stderr);
        fclose(input); return 2;
    }
    if (fread(memory.ram, 1, (size_t)length, input) != (size_t)length) {
        fclose(input); sim_memory_end(&memory); return 2;
    }
    fclose(input);
    FILE *out = trace ? fopen(trace, "w") : NULL;
    if (trace && !out) { perror(trace); sim_memory_end(&memory); return 2; }
    RISCVSIMCPUState cpu;
    INCore core;
    in_core_init(&core, &cpu, NULL, (size_t)length / 4, (uint32_t)base, NULL, 0);
    core.bus = sim_memory_bus(&memory);
    core.commit_trace = out;
    core.trace = stages;
    core.stop_pc = (uint32_t)stop; core.stop_pc_valid = has_stop;
    in_core_run(&core, cycles);
    int done = core.halted || (core.fetch_done && !core.decode.has_data &&
        !core.execute.has_data && !core.memory.has_data && !core.commit.has_data);
    int status = cpu.trapped ? 1 : done ? 0 : 3;
    fprintf(stderr, "backend=%s clock=%" PRIu64 " retired=%" PRIu64
        " status=%s reads=%" PRIu64 " writes=%" PRIu64
        " device_reads=%" PRIu64 " device_writes=%" PRIu64 "\n",
        backend, cpu.clock, core.retired, cpu.trapped ? "trap" : done ? "complete" : "timeout",
        memory.reads, memory.writes, memory.device_reads, memory.device_writes);
    if (out) {
        int failed = ferror(out);
        if (fclose(out)) failed = 1;
        if (failed) status = 2;
    }
    sim_memory_end(&memory);
    return status;
}
