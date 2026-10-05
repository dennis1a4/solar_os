"""Compile actual worker lifecycle with scheduler/allocation failure injection."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class TaskMemoryTest(unittest.TestCase):
    def test_stack_lifetime(self):
        source=(ROOT/'src/platform/imxrt1062/teensy41/ssh_platform.cpp').read_text()
        body=source[source.index('// One foreground worker'):source.rfind('#endif')]
        stubs=(ROOT/'tests/ports/task_memory_prefix.cpp').read_text()
        checks=(ROOT/'tests/ports/task_memory_suffix.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='teensy-task-memory-') as directory:
            path=Path(directory);(path/'test.cpp').write_text(stubs+body+checks)
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)
if __name__=='__main__':unittest.main()
