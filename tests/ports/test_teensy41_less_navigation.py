"""Check the actual pager's forward/backward wrapped-row traversal."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NavigationTest(unittest.TestCase):
    def test_previous_row_is_inverse_of_next(self):
        source = (ROOT / 'src/apps/solar_os_less.c').read_text()
        start = source.index('static size_t less_line_end(')
        end = source.index('static void less_write_inverse_line(', start)
        code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define LESS_TAB_WIDTH 4
static struct { const char *buffer; size_t len; } less_state;
'''+source[start:end]+r'''
int main(void) {
    const char *fixtures[] = {
        "", "a", "a\n", "a\r\n", "\n\n", "one\ntwo\nthree",
        "a long paragraph wraps into many visual rows and must be reversible.\nnext\n",
        "words\twith\ttabs and   multiple spaces\r\n\r\nlast line",
        "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "0123456789 0123456789 0123456789 0123456789\nlast line with words\n"
    };
    for (size_t f=0; f<sizeof(fixtures)/sizeof(fixtures[0]); ++f) {
        less_state.buffer=fixtures[f]; less_state.len=strlen(fixtures[f]);
        for (size_t cols=1; cols<=100; ++cols) {
            size_t positions[256], count=0, offset=0;
            positions[count++]=0;
            while (offset<less_state.len) {
                size_t next=less_next_visual_start(offset,cols);
                assert(next>offset && next<=less_state.len);
                assert(less_previous_visual_start(next,cols)==offset);
                assert(count<256);positions[count++]=next;offset=next;
            }
            while (count>1) {
                offset=less_previous_visual_start(offset,cols);
                assert(offset==positions[--count-1]);
            }
            assert(less_previous_visual_start(0,cols)==0);
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='teensy-less-') as directory:
            path = Path(directory)
            (path / 'test.c').write_text(code)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
