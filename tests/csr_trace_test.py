"""End-to-end CSR instruction and privilege-skeleton tests."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SIM = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]

MSCRATCH = 0x340
CYCLE = 0xC00
MISA = 0x301
MISA_VALUE = 0x40001100


def csr(f3, rd, addr, rs1):
    """SYSTEM opcode 0x73：f3=1/2/3 寄存器形式，5/6/7 立即数形式。"""
    return (addr << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | 0x73


def addi(rd, rs, value):
    return ((value & 4095) << 20) | (rs << 15) | (rd << 7) | 0x13


class CSRTraceTests(unittest.TestCase):
    def run_image(self, words, code=0):
        with tempfile.TemporaryDirectory() as d:
            image, trace = Path(d) / "image.bin", Path(d) / "trace.jsonl"
            image.write_bytes(struct.pack("<" + "I" * len(words), *words))
            result = subprocess.run(
                [str(SIM), "--bin", str(image), "--trace-json", str(trace)],
                capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, code, result.stderr)
            events = [json.loads(x) for x in trace.read_text().splitlines()] if trace.exists() else []
            return events, result.stderr

    def test_read_modify_write_sequence(self):
        words = [addi(1, 0, 0x234),
                 csr(1, 0, MSCRATCH, 1),   # csrrw x0, mscratch, x1
                 csr(2, 2, MSCRATCH, 0),   # csrrs x2, mscratch, x0（只读）
                 addi(3, 0, 0x0f0),
                 csr(2, 4, MSCRATCH, 3),   # csrrs x4, mscratch, x3
                 csr(3, 5, MSCRATCH, 3),   # csrrc x5, mscratch, x3
                 csr(5, 6, MSCRATCH, 7),   # csrrwi x6, mscratch, 7
                 csr(6, 7, MSCRATCH, 8),   # csrrsi x7, mscratch, 8
                 csr(7, 8, MSCRATCH, 1),   # csrrci x8, mscratch, 1
                 csr(2, 9, MSCRATCH, 0)]   # csrrs x9, mscratch, x0
        events, _ = self.run_image(words)
        self.assertEqual([e["type"] for e in events], ["commit"] * len(words))
        # 每条 CSR 指令写回的是「动作之前的旧值」，因此序列可证明读改写顺序正确。
        self.assertEqual([int(e["wdata"], 0) for e in events],
                         [564, 0, 564, 240, 564, 756, 516, 7, 15, 14])
        self.assertEqual(events[1]["wen"], 0)  # csrw 不写整数寄存器

    def test_write_readonly_csr_traps(self):
        words = [addi(1, 0, 1), csr(1, 0, CYCLE, 1)]  # csrrw x0, cycle, x1
        events, _ = self.run_image(words, code=1)
        self.assertEqual([e["type"] for e in events], ["commit", "trap"])
        self.assertEqual(events[-1]["cause"], 2)
        self.assertEqual(int(events[-1]["tval"], 0), words[1])
        self.assertEqual(events[-1]["seq"], 1)

    def test_readonly_csr_read_is_allowed(self):
        events, _ = self.run_image([csr(6, 1, CYCLE, 0)])  # csrrsi x1, cycle, 0
        self.assertEqual(events[-1]["type"], "commit")
        self.assertEqual(int(events[-1]["wdata"], 0), events[-1]["cycle"] - 1)

    def test_misa_and_counter(self):
        events, _ = self.run_image([csr(2, 1, MISA, 0)])
        self.assertEqual(int(events[0]["wdata"], 0), MISA_VALUE)
        events, _ = self.run_image([csr(2, 1, CYCLE, 0)])
        self.assertEqual(int(events[0]["wdata"], 0), events[0]["cycle"] - 1)


if __name__ == "__main__":
    unittest.main()
