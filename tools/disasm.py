"""RV32IMA 展示用反汇编。未知编码保留为 .word，不推测其含义。"""


def signed(value, bits):
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def disassemble(i, pc=0):
    op, f3, f7 = i & 127, (i >> 12) & 7, i >> 25
    rd, r1, r2 = (i >> 7) & 31, (i >> 15) & 31, (i >> 20) & 31
    imm = signed(i >> 20, 12)
    def target(offset, bits):
        return f'0x{(pc + signed(offset, bits)) & 0xffffffff:08x}'
    if op in (0x37, 0x17):
        return f'{"lui" if op == 0x37 else "auipc"} x{rd}, 0x{i >> 12:x}'
    if op == 0x6f:
        off = (i >> 31) << 20 | ((i >> 12) & 255) << 12 | ((i >> 20) & 1) << 11 | ((i >> 21) & 1023) << 1
        return f'jal x{rd}, {target(off, 21)}'
    if op == 0x67 and f3 == 0:
        return f'jalr x{rd}, {imm}(x{r1})'
    if op == 0x63:
        name = {0:'beq', 1:'bne', 4:'blt', 5:'bge', 6:'bltu', 7:'bgeu'}.get(f3)
        off = (i >> 31) << 12 | ((i >> 7) & 1) << 11 | ((i >> 25) & 63) << 5 | ((i >> 8) & 15) << 1
        if name:
            return f'{name} x{r1}, x{r2}, {target(off, 13)}'
    if op == 3:
        name = {0:'lb', 1:'lh', 2:'lw', 4:'lbu', 5:'lhu'}.get(f3)
        if name:
            return f'{name} x{rd}, {imm}(x{r1})'
    if op == 0x23 and f3 <= 2:
        off = signed((i >> 25) << 5 | ((i >> 7) & 31), 12)
        return f'{("sb", "sh", "sw")[f3]} x{r2}, {off}(x{r1})'
    if op == 0x13:
        name = {0:'addi', 2:'slti', 3:'sltiu', 4:'xori', 6:'ori', 7:'andi'}.get(f3)
        if name:
            return f'{name} x{rd}, x{r1}, {imm}'
        if f3 == 1 and f7 == 0:
            return f'slli x{rd}, x{r1}, {r2}'
        if f3 == 5 and f7 in (0, 32):
            return f'{"srai" if f7 else "srli"} x{rd}, x{r1}, {r2}'
    if op == 0x33:
        name = None
        if f7 == 0:
            name = ('add', 'sll', 'slt', 'sltu', 'xor', 'srl', 'or', 'and')[f3]
        elif f7 == 1:
            name = ('mul', 'mulh', 'mulhsu', 'mulhu', 'div', 'divu', 'rem', 'remu')[f3]
        elif f7 == 32:
            name = {0:'sub', 5:'sra'}.get(f3)
        if name:
            return f'{name} x{rd}, x{r1}, x{r2}'
    if op == 0x2f and f3 == 2:
        f5 = i >> 27
        name = {0:'amoadd', 1:'amoswap', 2:'lr', 3:'sc', 4:'amoxor', 8:'amoor',
                12:'amoand', 16:'amomin', 20:'amomax', 24:'amominu', 28:'amomaxu'}.get(f5)
        if name and (f5 != 2 or r2 == 0):
            suffix = ('', '.rl', '.aq', '.aqrl')[(i >> 25) & 3]
            operands = f'x{rd}, (x{r1})' if f5 == 2 else f'x{rd}, x{r2}, (x{r1})'
            return f'{name}.w{suffix} {operands}'
    if op == 15 and f3 == 0 and rd == r1 == 0:
        if i == 0x8330000f:
            return 'fence.tso'
        if i == 0x0100000f:
            return 'pause'
        if i >> 28 == 0:
            def mask(v):
                return ''.join(c for bit, c in ((8,'i'), (4,'o'), (2,'r'), (1,'w')) if v & bit) or '0'
            return f'fence {mask((i >> 24) & 15)}, {mask((i >> 20) & 15)}'
    if i == 0x73:
        return 'ecall'
    if i == 0x100073:
        return 'ebreak'
    return f'.word 0x{i:08x}'
