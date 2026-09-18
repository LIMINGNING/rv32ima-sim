"""End-to-end loader, retirement, fault and real TinyEMU iomem tests."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SIM = Path(sys.argv[1]).resolve()
WITH_MARSS = sys.argv[2] == "1"
sys.argv = sys.argv[:1]
BASE = 0x80000000


def imm(op, rd, f, rs, value):
    return ((value & 4095) << 20) | (rs << 15) | (f << 12) | (rd << 7) | op


def store(f, rs, base, offset):
    v = offset & 4095
    return ((v >> 5) << 25) | (rs << 20) | (base << 15) | (f << 12) | ((v & 31) << 7) | 0x23


class LoaderTraceTests(unittest.TestCase):
    def run_image(self, words, backend="flat", extra=(), code=0):
        with tempfile.TemporaryDirectory() as d:
            image, trace = Path(d) / "image.bin", Path(d) / "trace.jsonl"
            image.write_bytes(struct.pack("<" + "I" * len(words), *words))
            result = subprocess.run([str(SIM), "--bin", str(image), "--trace-json", str(trace),
                                     "--backend", backend, *extra], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, code, result.stderr)
            events = [json.loads(line) for line in trace.read_text().splitlines()] if trace.exists() else []
            return events, result.stderr

    def test_little_endian_arithmetic_and_x0(self):
        e, _ = self.run_image([0x00500093, 0x00700113, 0x002081b3, 0x00100013])
        self.assertEqual([int(x['wdata'], 0) for x in e], [5, 7, 12, 0])
        self.assertEqual(e[-1]['wen'], 0)
        self.assertEqual([x['seq'] for x in e], [1, 2, 3, 4])

    def test_memory_and_subword_mask(self):
        words = [0x80000237, imm(0x13, 1, 0, 0, -1), store(0, 1, 4, 256),
                 imm(3, 2, 0, 4, 256), imm(3, 3, 4, 4, 256),
                 store(1, 1, 4, 258), imm(3, 5, 5, 4, 258)]
        backends = ['flat', 'marss'] if WITH_MARSS else ['flat']
        traces = []
        for backend in backends:
            e, _ = self.run_image(words, backend)
            self.assertEqual(int(e[2]['mem_addr'], 0), BASE + 256)
            self.assertEqual(int(e[2]['mem_wdata'], 0), 255)
            self.assertEqual([int(e[i]['wdata'], 0) for i in (3, 4, 6)], [0xffffffff, 255, 65535])
            traces.append(e)
        self.assertTrue(all(e == traces[0] for e in traces))

    def test_illegal_is_trap_and_younger_write_cancelled(self):
        e, _ = self.run_image([0x00500093, 0xffffffff, 0x00700113], code=1)
        self.assertEqual([x['type'] for x in e], ['commit', 'trap'])
        self.assertEqual(e[-1]['cause'], 2)
        self.assertEqual(int(e[-1]['tval'], 0), 0xffffffff)
        self.assertEqual(e[-1]['seq'], 1)
        self.assertEqual(e[-1]['wen'], 0)

    def test_unmapped_and_unaligned(self):
        for words, cause in [([imm(3, 1, 2, 0, 0)], 5),
                             ([0x80000237, imm(3, 1, 2, 4, 1)], 4),
                             ([store(2, 0, 0, 0)], 7)]:
            e, _ = self.run_image(words, code=1)
            self.assertEqual(e[-1]['cause'], cause)
            self.assertEqual(e[-1]['mem_wmask'], 0)

    def test_branch_flush_and_stop_pc(self):
        e, _ = self.run_image([0x00000463, 0xffffffff, 0x00500093])
        self.assertEqual([int(x['pc'], 0) for x in e], [BASE, BASE + 8])
        e, _ = self.run_image([0x00500093, 0x00700113], extra=['--stop-pc', hex(BASE)])
        self.assertEqual(len(e), 1)

    def test_timeout(self):
        e, log = self.run_image([0x0000006f], extra=['--max-cycles', '20'], code=3)
        self.assertGreater(len(e), 0)
        self.assertIn('status=timeout', log)

    def test_trace_cannot_overwrite_image_alias(self):
        with tempfile.TemporaryDirectory() as d:
            image, alias = Path(d) / 'image.bin', Path(d) / 'alias'
            image.write_bytes(struct.pack('<I', 0x13))
            alias.symlink_to(image)
            r = subprocess.run([str(SIM), '--bin', str(image), '--trace-json', str(alias)], capture_output=True)
            self.assertEqual(r.returncode, 2)
            self.assertEqual(image.read_bytes(), struct.pack('<I', 0x13))

    def test_reject_bad_image_and_options(self):
        for data in (b'', b'abc', b'12345'):
            with tempfile.TemporaryDirectory() as d:
                image = Path(d) / 'bad.bin'
                image.write_bytes(data)
                r = subprocess.run([str(SIM), '--bin', str(image)], capture_output=True)
                self.assertEqual(r.returncode, 2)
        self.run_image([0x13], extra=['--base', '0x80000001'], code=2)
        self.run_image([0x13], extra=['--max-cycles', '-1'], code=2)

    @unittest.skipUnless(WITH_MARSS, 'optional MARSS iomem backend')
    def test_real_iomem_mmio_once_and_wrong_path_cancel(self):
        words = [0x10000237, imm(0x13, 1, 0, 0, 42), store(2, 1, 4, 0),
                 imm(3, 2, 2, 4, 0), 0x00000463, store(2, 0, 4, 0), 0x13]
        e, log = self.run_image(words, 'marss')
        self.assertEqual(int(e[3]['wdata'], 0), 42)
        self.assertIn('device_reads=1 device_writes=1', log)
        self.assertNotIn(BASE + 20, [int(x['pc'], 0) for x in e])

    @unittest.skipUnless(WITH_MARSS, 'optional MARSS iomem backend')
    def test_fault_cancels_younger_device_read_and_write(self):
        for younger in (imm(3, 2, 2, 4, 0), store(2, 0, 4, 0)):
            e, log = self.run_image([0x10000237, imm(3, 1, 2, 0, 0), younger], 'marss', code=1)
            self.assertEqual(e[-1]['type'], 'trap')
            self.assertIn('device_reads=0 device_writes=0', log)


if __name__ == '__main__':
    unittest.main()
