"""Python 工具 CLI 回归：格式往返、差异、坏输入、HTML。"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.log = self.directory / 'trace.log'
        self.log.write_text('cycle=1 IF pc=0x80000000 insn=0x00000013\n'
                            'cycle=5 WB pc=0x80000000 insn=0x00000013\n'
                            'clock = 5, retired = 1\n')

    def cli(self, tool, *args, code=0):
        result = subprocess.run([sys.executable, str(ROOT/'tools'/tool), *map(str,args)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def test_round_trip(self):
        csv = self.directory/'trace.csv'
        jsonl = self.directory/'trace.jsonl'
        self.cli('trace.py', self.log, '-o', csv)
        self.cli('trace.py', csv, '-o', jsonl)
        self.cli('diff.py', '--actual', jsonl, '--expected', self.log)
        self.assertEqual(len(jsonl.read_text().splitlines()), 2)

    def test_difference_and_wb_timing(self):
        other = self.directory/'other.log'
        other.write_text(self.log.read_text().replace('cycle=5','cycle=6'))
        result = self.cli('diff.py','--actual',other,'--expected',self.log,code=1)
        self.assertIn('第 2 个事件',result.stdout)
        self.cli('diff.py','--actual',other,'--expected',self.log,'--mode','wb')
        other.write_text(other.read_text().replace('00000013','00100093'))
        self.cli('diff.py','--actual',other,'--expected',self.log,'--mode','wb',code=1)

    def test_truncated(self):
        other = self.directory/'short.log'
        other.write_text(self.log.read_text().splitlines()[0]+'\n')
        self.cli('diff.py','--actual',other,'--expected',self.log,code=1)
        self.cli('diff.py','--actual',other,'--expected',self.log,'--mode','wb',code=2)

    def test_invalid(self):
        for text in ('', 'garbage\n', 'cycle=0 IF pc=0x0 insn=0x13\n',
                     'cycle=1 IF pc=0x0 insn=0x13\n'*2):
            self.log.write_text(text)
            self.cli('trace.py',self.log,'-o',self.directory/'out.csv',code=2)
        invalid=self.directory/'bad.jsonl'
        invalid.write_text(json.dumps(dict(cycle=True,stage='IF',pc=0,insn=19)))
        self.cli('trace.py',invalid,'-o',self.directory/'out.csv',code=2)

    def test_html_range(self):
        output=self.directory/'nested'/'view.html'
        self.cli('view_trace.py',self.log,'-o',output,'--start',5,'--end',5)
        text=output.read_text()
        self.assertIn('<th>5</th>',text)
        self.assertNotIn('<th>1</th>',text)
        self.assertIn('0x80000000',text)
        self.cli('view_trace.py',self.log,'--start',7,code=2)


if __name__ == '__main__':
    unittest.main()
