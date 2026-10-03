"""Embedded manual must expose the port's commands without promising ESP services."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
GEN=module('manual_generator','scripts/generate_manual.py')
PORT=module('teensy_manual','scripts/ports/teensy41_manual.py')

class ManualTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pages=PORT.select_pages(GEN.load_pages(ROOT/'doc/manual',ROOT/'packages/solar_os_packages.toml'),GEN.markdown_to_terminal_text)
        cls.by_id={p['id']:p for p in cls.pages}
    def test_workstation_topics(self):
        for name in ('man','watch','session','sessions','fg','close','df','top','pwd','date','time','zip','unzip','ping','netscan','ntp'):
            self.assertIn('command.'+name,self.by_id)
    def test_composition_topics(self):
        for name in ('grep', 'head', 'wc'):
            self.assertIn('command.'+name, self.by_id)
        self.assertIn('8 KiB', self.by_id['command.echo']['body'])
        self.assertIn('128 KiB', self.by_id['command.mem']['body'])

    def test_hardware_topics(self):
        for name in ("gpio", "i2c", "spi", "uart", "expansion"):
            self.assertIn("command."+name,self.by_id)
        for name in ("io", "com"):
            self.assertIn("app."+name,self.by_id)
        self.assertIn("UART3 conflicts",self.by_id["command.uart"]["body"])
        self.assertIn("Ctrl+Z",self.by_id["app.com"]["body"])

    def test_ramfs_limits(self):
        text=self.by_id["command.ramfs"]["body"]
        for phrase in ("PSRAM", "reboot/unmount", "Open files/directories", "512 KiB"):
            self.assertIn(phrase,text)

    def test_excludes_unavailable_services(self):
        for name in ('wifi','espnow','ota','nvs'):
            self.assertNotIn('command.'+name,self.by_id)
    def test_sessions_document_retention_and_limits(self):
        text=self.by_id['command.session']['body']
        self.assertIn('not integrated',text)
        self.assertIn('Ctrl+Z',text)
        self.assertIn('discards unsaved',text)
    def test_aliases(self):
        for name in ('man','watch','pwd','session','sessions','top'):
            self.assertIn(name,self.by_id['command.'+name]['aliases'])
    def test_help_documents_offline_limit(self):
        self.assertIn('not integrated',self.by_id['command.help']['body'])
    def test_all_allowlisted_apps_have_pages(self):
        for name in PORT.APP_NAMES:
            self.assertIn('app.' + name, self.by_id)

    def test_view_matches_teensy_decoders_and_controls(self):
        text = self.by_id['app.view']['body']
        for phrase in ('PNG', 'JPEG', 'GIF', 'BMP', 'PNM P1-P6',
                       'WebP is unavailable', '0 selects actual size',
                       '1 selects fit', 'LCD shell', 'PSRAM'):
            self.assertIn(phrase, text)

    def test_hexedit_discoverable(self):
        self.assertIn('app.hexedit',self.by_id)

if __name__=='__main__':unittest.main()
