"""Exercise actual audio ring code with interrupt and allocation failure injection."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AudioMemoryTest(unittest.TestCase):
    def test_ring_lifetime(self):
        source = (ROOT / 'src/platform/imxrt1062/teensy41/audio_output.cpp').read_text()
        body = source[source.index('// One foreground producer'):source.index('extern "C" void solar_os_shell_cmd_audio')]
        prefix = (ROOT / 'tests/ports/audio_memory_prefix.cpp').read_text()
        suffix = (ROOT / 'tests/ports/audio_memory_suffix.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='teensy-audio-memory-') as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(prefix + body + suffix)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-g',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_console_admission(self):
        dual = (ROOT / 'src/platform/imxrt1062/teensy41/shell_dual.cpp').read_text()
        retained = (ROOT / 'src/platform/imxrt1062/teensy41/shell_retained.h').read_text()
        app = dual[dual.index('static bool audio_app('):dual.index('extern "C" uint32_t sk_python_random_seed')]
        busy = retained[retained.index('static bool console_has_audio('):retained.index('static unsigned console_entries')]
        code = r'''#include <cassert>
#include <cstring>
#define SK_AUDIO_PLAYER 1
#define SK_TELNETD 1
struct solar_os_app_t {const char *name;};
struct Frame {const solar_os_app_t *app;Frame *parent;};
struct Console {Frame *frame;Frame *retained[2];};
static Console consoles[2]{},remote_console{},*audio_owner;
static Console &active(){return consoles[0];}
static bool diagnostic;
extern "C" bool sk_audio_diagnostic_busy(){return diagnostic;}
''' + app + busy + r'''
int main(){
 solar_os_app_t synth{"synth"},calc{"calc"},recorder{"arecord"};Frame frame{&synth,nullptr};
 assert(sk_app_allowed(&synth) && !sk_console_audio_busy());
 diagnostic=true;assert(!sk_app_allowed(&synth) && sk_app_allowed(&calc));diagnostic=false;
 consoles[1].frame=&frame;assert(sk_console_audio_busy() && !sk_app_allowed(&synth));
 consoles[1].frame=nullptr;consoles[1].retained[0]=&frame;
 assert(sk_console_audio_busy() && !sk_app_allowed(&synth));
 consoles[1].retained[0]=nullptr;remote_console.frame=&frame;
 assert(sk_console_audio_busy() && !sk_app_allowed(&synth));
 assert(!sk_console_recorder_busy());frame.app=&recorder;
 assert(sk_console_recorder_busy());remote_console.frame=nullptr;
 consoles[1].retained[0]=&frame;assert(sk_console_recorder_busy());
 consoles[1].retained[0]=nullptr;
 assert(!sk_console_recorder_busy() && !sk_console_audio_busy() && sk_app_allowed(&synth));
}
'''
        with tempfile.TemporaryDirectory(prefix='teensy-audio-owner-') as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(code)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
