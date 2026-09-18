#include "memory.h"
#include <stdlib.h>
#include <string.h>

#ifdef SIM_WITH_MARSS
#include "cutils.h"
#include "iomem.h"

/* A deliberately tiny test device, NOT a UART/PLIC implementation.
 * Reads return a latch and count consumption; writes replace the latch. */
static uint32_t device_read(void *opaque, uint32_t offset, int log2)
{
    SimMemory *m = opaque;
    (void)offset; (void)log2;
    m->device_reads++;
    return m->device_value;
}
static void device_write(void *opaque, uint32_t offset, uint32_t value, int log2)
{
    SimMemory *m = opaque;
    (void)offset; (void)log2;
    m->device_writes++;
    m->device_value = value;
}
#endif

int sim_memory_init(SimMemory *m, uint32_t base, size_t size, int marss)
{
    memset(m, 0, sizeof(*m));
    if (!size || (size & 4095) || (base & 4095) ||
        (uint64_t)base + size > UINT64_C(0x100000000)) return -1;
    if (marss && base < (uint64_t)SIM_TEST_DEVICE + 4096 &&
        (uint64_t)base + size > SIM_TEST_DEVICE) return -1;
    m->base = base; m->size = size;
    if (marss) {
#ifdef SIM_WITH_MARSS
        PhysMemoryMap *map = phys_mem_map_init();
        m->map = map;
        PhysMemoryRange *ram = cpu_register_ram(map, base, size, 0);
        m->ram = ram->phys_mem;
        cpu_register_device(map, SIM_TEST_DEVICE, 4096, m,
                            device_read, device_write, DEVIO_SIZE32);
#else
        return -1;
#endif
    } else m->ram = calloc(1, size);
    return m->ram ? 0 : -1;
}

void sim_memory_end(SimMemory *m)
{
#ifdef SIM_WITH_MARSS
    if (m->map) { phys_mem_map_end(m->map); return; }
#endif
    free(m->ram);
}

static int probe(void *opaque, uint32_t addr, unsigned width, int access)
{
    SimMemory *m = opaque;
    if ((width != 1 && width != 2 && width != 4) || (addr & (width - 1)) ||
        (uint64_t)addr + width > UINT64_C(0x100000000)) return -1;
#ifdef SIM_WITH_MARSS
    if (m->map) {
        PhysMemoryRange *r = get_phys_mem_range(m->map, addr);
        if (!r || (uint64_t)addr + width > r->addr + r->size) return -1;
        if (r->is_ram)
            return access == 1 && (r->devram_flags & DEVRAM_FLAG_ROM) ? -1 : 0;
        unsigned log2 = width == 4 ? 2 : width == 2 ? 1 : 0;
        if (access == 2 || !(r->devio_flags & (1 << log2))) return -1;
        return access == 1 ? (r->write_func ? 0 : -1) : (r->read_func ? 0 : -1);
    }
#else
    (void)access;
#endif
    return addr >= m->base && (uint64_t)addr - m->base + width <= m->size ? 0 : -1;
}

static int read_bus(void *opaque, uint32_t addr, unsigned width, uint32_t *value)
{
    SimMemory *m = opaque;
    if (probe(m, addr, width, 0)) return -1;
    m->reads++;
    uint8_t *ptr;
#ifdef SIM_WITH_MARSS
    if (m->map) {
        PhysMemoryRange *r = get_phys_mem_range(m->map, addr);
        if (!r->is_ram) {
            *value = r->read_func(r->opaque, (uint32_t)(addr - r->addr),
                                  width == 4 ? 2 : width == 2 ? 1 : 0);
            return 0;
        }
        ptr = r->phys_mem + (addr - r->addr);
    } else
#endif
        ptr = m->ram + (addr - m->base);
    *value = 0;
    for (unsigned i = 0; i < width; i++) *value |= (uint32_t)ptr[i] << (i * 8);
    return 0;
}

static int write_bus(void *opaque, uint32_t addr, unsigned width, uint32_t value)
{
    SimMemory *m = opaque;
    if (probe(m, addr, width, 1)) return -1;
    m->writes++;
    uint8_t *ptr;
#ifdef SIM_WITH_MARSS
    if (m->map) {
        PhysMemoryRange *r = get_phys_mem_range(m->map, addr);
        if (!r->is_ram) {
            r->write_func(r->opaque, (uint32_t)(addr - r->addr), value,
                           width == 4 ? 2 : width == 2 ? 1 : 0);
            return 0;
        }
        ptr = r->phys_mem + (addr - r->addr);
        phys_mem_set_dirty_bit(r, addr - r->addr);
    } else
#endif
        ptr = m->ram + (addr - m->base);
    for (unsigned i = 0; i < width; i++) ptr[i] = (uint8_t)(value >> (i * 8));
    return 0;
}

SimBus sim_memory_bus(SimMemory *m)
{
    return (SimBus){m, probe, read_bus, write_bus};
}
