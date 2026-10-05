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

    def test_actual_registry_coverage_and_conditions(self):
        # Evaluate the real registry gates, not another copy of its app list.
        # Full workstation and reduced USB-shell profiles catch missing topics
        # and pages accidentally exposed for features that are compiled out.
        import re
        import subprocess
        config = ROOT / 'src/platform/imxrt1062/teensy41/compat/solar_os_config.h'
        registries = {
            'app': GEN.registry_conditions(ROOT / 'src/apps/solar_os_app_registry.c',
                'registered_apps[]', r'APP_(?:FILE_)?ENTRY\("([a-z0-9-]+)"\s*,'),
            'command': GEN.registry_conditions(ROOT / 'src/apps/solar_os_shell.c',
                'shell_builtin_commands[]', r'\{"([a-z0-9-]+)"\s*,'),
        }
        flags = set(re.findall(r'\bSK_[A-Z0-9_]+\b', config.read_text() +
                    ' '.join(gate for r in registries.values() for gate in r.values())))
        def active(conditions, full):
            prefix = '#define SOLAR_OS_PLATFORM_IMXRT1062 1\n#define SOLAR_OS_SHELL_CORE_ONLY 1\n'
            prefix += ''.join(f'#define {flag} {int(full or flag == "SK_UPSTREAM_SHELL")}\n' for flag in sorted(flags))
            prefix += f'#include "{config}"\n'
            source = prefix + '\n'.join(f'#if {gate or "1"}\n"{name}"\n#endif' for name, gate in conditions.items())
            result = subprocess.run(['cpp', '-P', '-'], input=source, text=True, capture_output=True, check=True)
            return set(re.findall(r'^"([^"]+)"$', result.stdout, re.M))
        for full in (False, True):
            for kind, registry in registries.items():
                pages = {p['id'].partition('.')[2]: p.get('condition', '')
                         for p in self.pages if p['section'] == kind}
                self.assertEqual(active(registry, full), active(pages, full), (kind, full))

    def test_all_pages_embedded_without_download_placeholder(self):
        header = GEN.render_header(self.pages, ROOT / 'doc/manual', embed_all=True)
        self.assertNotIn('connect Wi-Fi', header)
        self.assertNotIn('downloadable manual', header)
        self.assertNotIn('.body = SOLAR_OS_MANUAL_DOWNLOAD_NOTICE', header)
        for index, page in enumerate(self.pages):
            self.assertIn(f'.body = SOLAR_OS_MANUAL_SETUP_{index}', header)
            self.assertIn(GEN.c_string(page['body']), header)
        # Teensy embedding does not change the upstream ESP release policy.
        upstream = GEN.render_header(self.pages, ROOT / 'doc/manual')
        self.assertIn('connect Wi-Fi', upstream)
        self.assertIn('.body = SOLAR_OS_MANUAL_DOWNLOAD_NOTICE', upstream)

    def test_no_esp_instructions_in_selected_pages(self):
        for page in self.pages:
            for stale in ('ESP32', 'NVS', 'Alt+Tab', 'help update', 'connect Wi-Fi', 'dual-core'):
                self.assertNotIn(stale, page['body'], page['id'])
            self.assertNotRegex(page['body'], r'(?<!/flash)/\.ssh/')

    def test_hardware_and_storage_contracts(self):
        for topic, phrases in {
            'app.synth': ('eight-voice', 'SGTL5000', '220 ms'),
            'app.arecord': ('-i mic', '44100 Hz', 'one-hour'),
            'app.webradio': ('Ethernet', 'text station list', '128 KiB', '/flash/.solar/webradio/catalog.bin'),
            'app.ltop': ('One Cortex-M7 core', 'FREE', 'headroom'),
            'app.ssh': ('/flash/.ssh', 'Ctrl+Z'),
            'command.sshkey': ('/flash/.ssh/id_rsa',),
            'command.lcd': ('lcd font 1|2|3', 'lcd color FG BG'),
            'command.network': ('/flash/.shell/startup', 'Wi-Fi is not integrated'),
            'app.clock': ('No RTC alarm interrupt',),
            'app.scope': ('legacy bench firmware disables',),
        }.items():
            for phrase in phrases:
                self.assertIn(phrase, self.by_id[topic]['body'], topic)

if __name__=='__main__':unittest.main()
