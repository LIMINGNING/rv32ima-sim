#ifndef SIM_MEMORY_H
#define SIM_MEMORY_H
#include "../core/core.h"

/* Optional MARSS backend links upstream iomem.c directly, not the MARSS CPU. */
typedef struct SimMemory {
    uint32_t base;
    size_t size;
    uint8_t *ram;
    void *map;
    uint64_t reads, writes, device_reads, device_writes;
    uint32_t device_value;
} SimMemory;

int sim_memory_init(SimMemory *m, uint32_t base, size_t size, int marss);
void sim_memory_end(SimMemory *m);
SimBus sim_memory_bus(SimMemory *m);
#define SIM_TEST_DEVICE UINT32_C(0x10000000)
#endif
