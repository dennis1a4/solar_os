"""Exercise upstream player transition functions with Teensy repeat policy."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class PlayerControlsTest(unittest.TestCase):
    def test_transitions(self):
        source = (ROOT / 'src/apps/solar_os_player.c').read_text()
        functions = []
        for name in ('player_play_offset', 'player_reap_finished'):
            functions.append(re.search(r'^static void ' + name + r'\([\s\S]*?^}', source, re.M)[0])
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SOLAR_OS_PLATFORM_IMXRT1062 1
#define SOLAR_OS_TASK_STOP_WAIT_MS 1000
#define ESP_OK 0
#define PLAYER_ERROR 7
typedef void *TaskHandle_t;
static struct {TaskHandle_t task; bool task_done,natural_completion,redraw;
    size_t active_index,cursor,track_count; unsigned repeat;} player;
static unsigned plays,deletes,waits,cycles;
static size_t selected;
static void player_set_message(const char *s){(void)s;}
static int player_play_index(size_t i){selected=i;++plays;return ESP_OK;}
static bool solar_os_task_wait_done(TaskHandle_t t,volatile bool *done,unsigned ms){
    assert(t && *done && ms==1000);return ++waits%2==0;
}
static void vTaskDelay(unsigned ticks){assert(ticks==1);}
static void solar_os_task_delete_external(TaskHandle_t t){assert(t && waits%2==0);++deletes;}
static bool sk_player_folder_shuffled(void){return true;}
static size_t sk_player_folder_shuffle(bool s,size_t keep){assert(s && keep==SIZE_MAX);++cycles;return 0;}
''' + '\n'.join(functions) + r'''
int main(void){
 for(unsigned mode=0;mode<3;++mode) for(size_t i=0;i<3;++i){
    player.task=(void *)1;player.task_done=true;player.natural_completion=true;
    player.track_count=3;player.active_index=i;player.repeat=mode;
    unsigned before=plays;player_reap_finished();assert(!player.task);
    if(mode==0 && i==2)assert(plays==before);
    else {assert(plays==before+1);assert(selected==(mode==2?i:(i+1)%3));}
 }
 assert(deletes==9 && cycles==1);
 player.task=(void *)1;player.task_done=true;player.natural_completion=false;
 unsigned before=plays;player_reap_finished();assert(plays==before && deletes==10);
 player.task=NULL;player.track_count=3;player.cursor=0;player_play_offset(-1);assert(selected==2);
 player.task=(void *)1;player.active_index=2;player_play_offset(1);assert(selected==0);
 player.track_count=0;before=plays;player_play_offset(1);assert(plays==before);
}
'''
        with tempfile.TemporaryDirectory(prefix='player-controls-') as directory:
            path = Path(directory)
            (path / 'test.c').write_text(code)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.c'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

if __name__ == '__main__':
    unittest.main()
