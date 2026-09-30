"""Host checks for the guided runner's serial protocol and result handling."""
import importlib.util
from pathlib import Path
import shlex
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('hotplug', Path(__file__).resolve().parents[2] / 'scripts/ports/test_teensy41_hotplug.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)


class Port:
    def __init__(self, chunks):
        self.chunks, self.writes = iter(chunks), []

    def write(self, data):
        self.writes.append(data)

    def read(self, size):
        return next(self.chunks, b'')


class Protocol(unittest.TestCase):
    def console(self, chunks):
        return h.Console(Port(chunks), {'commands': []})

    def test_fragmented_ansi_prompt(self):
        c = self.console([b'SD: /sd mounted\r\n\x1b[1moperator@teen', b'sy:/flash ', b''])
        self.assertIn('/sd mounted', c.command('sd status'))
        self.assertEqual(c.port.writes, [b'sd status\r'])
        self.assertEqual(len(c.report['commands']), 1)

    def test_traceback_is_failure(self):
        c = self.console([b'Traceback (most recent call last):\r\nOSError: 19\r\n>>> '])
        with self.assertRaises(AssertionError):
            c.py('f.read()')
        self.assertTrue(c.python)

    def test_lcd_preserves_python_escapes(self):
        c = self.console([b'user@solar:/ '])
        command = "exec('try:\\n f.read(1)\\nexcept OSError: print(42)')"
        c.lcd(command)
        self.assertEqual(shlex.split(c.port.writes[0].decode()), ['lcd', 'send', command])

    def test_status_does_not_misread_unmounted(self):
        self.assertFalse(h.mounted('sd', 'SD: unavailable (close stale file handles)'))
        self.assertFalse(h.mounted('usb', 'USB: connected, not mounted'))
        self.assertTrue(h.mounted('sd', 'SD: /sd mounted, 0 open handles'))
        self.assertTrue(h.mounted('usb', 'USB: /usb, FAT32, 15260 MiB, 0 open handles'))

    def test_echoed_marker_does_not_pass(self):
        c = self.console([b'echo Reconnect_123\r\nuser@solar:/ ', b'Reconnect_123\r\nuser@solar:/ '])
        with patch.object(h.time, 'sleep'):
            c.marker('Reconnect_123')
        self.assertEqual(len(c.port.writes), 2)

    def test_abort_is_recorded(self):
        report = {'operator': []}
        with patch('builtins.input', return_value='no'), self.assertRaises(AssertionError):
            h.operator(report, 'Remove SD')
        self.assertEqual(report['operator'][0]['reply'], 'no')

    def test_timeout_preserves_transcript(self):
        c = self.console([])
        with patch.object(h.time, 'monotonic', side_effect=[0, 31]), self.assertRaises(TimeoutError):
            c.command('sd status')
        self.assertTrue(c.report['commands'][0]['timeout'])


if __name__ == '__main__':
    unittest.main()
