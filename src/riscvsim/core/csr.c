#include "csr.h"

/* CSR 地址位编码：addr[11:10] 为读写权限，addr[9:8] 为最低可访问特权级。 */
static uint32_t rw_perm(uint32_t addr)
{
    return (addr >> 10) & 3u;
}

static uint32_t min_priv(uint32_t addr)
{
    return (addr >> 8) & 3u;
}

int csr_permitted(uint32_t addr, uint32_t priv, int write)
{
    if (addr > 0xfffu)
        return -1;
    uint32_t low = min_priv(addr);
    if (low == 2u)                      /* 保留的特权级编码，视为非法 */
        return -1;
    if (priv < low)                     /* 当前特权级不足 */
        return -1;
    if (write && rw_perm(addr) >= 2u)   /* 只读 CSR 不可写 */
        return -1;
    return 0;
}

/* 计数器类 CSR 在 U 模式访问时还需 mcounteren 对应位为 1。 */
static int counter_enabled(const struct RISCVSIMCPUState *cpu, uint32_t addr)
{
    if (cpu->priv != PRIV_U)
        return 1;
    uint32_t bit = 0;
    switch (addr)
    {
    case CSR_CYCLE: case CSR_CYCLEH:     bit = COUNTER_CYCLE; break;
    case CSR_TIME: case CSR_TIMEH:       bit = COUNTER_TIME; break;
    case CSR_INSTRET: case CSR_INSTRETH: bit = COUNTER_INSTRET; break;
    default:                             return 1;
    }
    return (cpu->csr[CSR_MCOUNTEREN] & bit) != 0;
}

int csr_read(const struct RISCVSIMCPUState *cpu, uint32_t addr, uint32_t *value)
{
    if (csr_permitted(addr, cpu->priv, 0) || !counter_enabled(cpu, addr))
        return -1;
    switch (addr)
    {
    case CSR_MISA:
        *value = MISA_VALUE;
        return 0;
    case CSR_MVENDORID: case CSR_MARCHID: case CSR_MIMPID: case CSR_MHARTID:
        *value = 0;
        return 0;
    /* M1 尚未接入 CLINT，time 暂与 cycle 同源。 */
    case CSR_CYCLE: case CSR_TIME:
        *value = (uint32_t)cpu->clock;
        return 0;
    case CSR_CYCLEH: case CSR_TIMEH:
        *value = (uint32_t)(cpu->clock >> 32);
        return 0;
    case CSR_INSTRET:
        *value = (uint32_t)cpu->instret;
        return 0;
    case CSR_INSTRETH:
        *value = (uint32_t)(cpu->instret >> 32);
        return 0;
    default:
        *value = cpu->csr[addr];
        return 0;
    }
}

void csr_store(struct RISCVSIMCPUState *cpu, uint32_t addr, uint32_t value)
{
    if (addr > 0xfffu)
        return;
    switch (addr)
    {
    case CSR_MISA:
    case CSR_MVENDORID: case CSR_MARCHID: case CSR_MIMPID: case CSR_MHARTID:
    case CSR_CYCLE: case CSR_TIME: case CSR_INSTRET:
    case CSR_CYCLEH: case CSR_TIMEH: case CSR_INSTRETH:
        return;   /* 只读或只读计算类，写入被忽略（WARL 语义） */
    default:
        cpu->csr[addr] = value;
        return;
    }
}

int csr_access(struct RISCVSIMCPUState *cpu, uint32_t addr, unsigned funct3,
               unsigned rs1_field, uint32_t src, uint32_t *old)
{
    uint32_t value;
    if (csr_read(cpu, addr, &value))
        return -1;
    /* CSRRW/CSRRWI 总是写；CSRRS/CSRRC/CSRRSI/CSRRCI 在 rs1/uimm 为 0 时只读。 */
    int write = (funct3 == CSR_F3_RW || funct3 == CSR_F3_RWI) ? 1 : (rs1_field != 0);
    if (write)
    {
        if (csr_permitted(addr, cpu->priv, 1))
            return -1;   /* 写只读 CSR 为非法指令 */
        uint32_t next;
        switch (funct3)
        {
        case CSR_F3_RW:  case CSR_F3_RWI: next = src; break;
        case CSR_F3_RS:  case CSR_F3_RSI: next = value | src; break;
        case CSR_F3_RC:  case CSR_F3_RCI: next = value & ~src; break;
        default:                          return -1;
        }
        csr_store(cpu, addr, next);
    }
    *old = value;
    return 0;
}
