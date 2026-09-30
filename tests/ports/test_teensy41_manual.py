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
        for name in ('man','watch','session','sessions','df','top','pwd','date','time','zip','unzip'):
            self.assertIn('command.'+name,self.by_id)
    def test_excludes_unavailable_services(self):
        for name in ('wifi','espnow','ota','nvs','job','jobs','fg'):
            self.assertNotIn('command.'+name,self.by_id)
    def test_sessions_dont_promise_retained_apps(self):
        text=self.by_id['command.session']['body']
        self.assertIn('not integrated',text)
        self.assertNotIn('session create',text)
    def test_aliases(self):
        for name in ('man','watch','pwd','session','sessions','top'):
            self.assertIn(name,self.by_id['command.'+name]['aliases'])
    def test_help_documents_offline_limit(self):
        self.assertIn('not integrated',self.by_id['command.help']['body'])
    def test_hexedit_discoverable(self):
        self.assertIn('app.hexedit',self.by_id)

if __name__=='__main__':unittest.main()
