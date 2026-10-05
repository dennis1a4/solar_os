#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "player_folder.h"
#include "solar_os_memory.h"
static unsigned live;
static int fail_after=-1;
void *solar_os_memory_alloc(size_t n, solar_os_memory_class_t cls, const char *tag) {
    (void)tag; assert(cls==SOLAR_OS_MEMORY_EXTERNAL_REQUIRED);
    if (fail_after==0) return NULL;
    if (fail_after>0) --fail_after;
    void *p=malloc(n); if(p) ++live; return p;
}
void solar_os_memory_free(void *p) { if(p){assert(live);--live;free(p);} }
uint32_t sk_python_random_seed(void) {return 12345;}
esp_err_t solar_os_storage_join_path(const char *a,const char *b,char *out,size_t size) {
    int n=snprintf(out,size,"%s/%s",a,b);return n>=0 && (size_t)n<size?ESP_OK:ESP_ERR_INVALID_SIZE;
}
static void file(const char *root,const char *name,bool create) {
    char path[512];snprintf(path,sizeof(path),"%s/%s",root,name);
    if(create) {FILE *f=fopen(path,"wb");assert(f);assert(fclose(f)==0);}
    else assert(unlink(path)==0);
}
int main(void) {
    char root[]="/tmp/solaros-player-folder-XXXXXX";assert(mkdtemp(root));
    assert(sk_player_folder_open(root)==ESP_ERR_NOT_FOUND && !live);
    file(root,"z.mp3",true);file(root,"Alpha.WAV",true);file(root,"beta.MP3",true);
    file(root,"ignored.txt",true);file(root,".hidden.mp3",true);
    char child[512];snprintf(child,sizeof(child),"%s/subdir.mp3",root);assert(mkdir(child,0700)==0);
    file(child,"nested.mp3",true);
    assert(sk_player_folder_open(root)==ESP_OK && solar_os_player_playlist_count()==3);
    solar_os_player_track_t t,keep;
    assert(solar_os_player_playlist_get(0,&t) && strstr(t.path,"Alpha.WAV"));
    assert(solar_os_player_playlist_get(1,&keep) && strstr(keep.path,"beta.MP3"));
    assert(!solar_os_player_playlist_get(3,&t));
    assert(sk_player_folder_shuffle(true,1)==0);
    assert(solar_os_player_playlist_get(0,&t) && !strcmp(t.path,keep.path));
    assert(sk_player_folder_shuffle(false,0)==1);
    assert(solar_os_player_playlist_get(1,&t) && !strcmp(t.path,keep.path));
    for(unsigned cycle=0;cycle<100;++cycle) {
        sk_player_folder_shuffle(true,SIZE_MAX);
        char seen[3][SOLAR_OS_STORAGE_PATH_MAX];
        for(unsigned i=0;i<3;++i) {
            assert(solar_os_player_playlist_get(i,&t));strcpy(seen[i],t.path);
            for(unsigned j=0;j<i;++j)assert(strcmp(seen[i],seen[j]));
        }
    }
    sk_player_folder_close();assert(!live);
    file(root,"z.mp3",false);file(root,"Alpha.WAV",false);file(root,"beta.MP3",false);
    file(root,"ignored.txt",false);file(root,".hidden.mp3",false);
    file(child,"nested.mp3",false);assert(rmdir(child)==0);
    for(unsigned i=0;i<64;++i) {char name[20];snprintf(name,sizeof(name),"%03u.mp3",i);file(root,name,true);}
    for(int i=0;i<4;++i) {
        fail_after=i;assert(sk_player_folder_open(root)==ESP_ERR_NO_MEM && !live);
    }
    fail_after=-1;
    assert(sk_player_folder_open(root)==ESP_OK && solar_os_player_playlist_count()==64);
    assert(sk_player_folder_open("/no/such/folder")==ESP_ERR_NOT_FOUND && !live);
    for(unsigned i=64;i<513;++i) {char name[20];snprintf(name,sizeof(name),"%03u.mp3",i);file(root,name,true);}
    assert(sk_player_folder_open(root)==ESP_ERR_INVALID_SIZE && !live);
    file(root,"512.mp3",false);
    assert(sk_player_folder_open(root)==ESP_OK && solar_os_player_playlist_count()==512);
    sk_player_folder_shuffle(true,SIZE_MAX);
    bool found[512]={0};
    for(unsigned i=0;i<512;++i) {
        assert(solar_os_player_playlist_get(i,&t));
        unsigned n=(unsigned)atoi(strrchr(t.path,'/')+1);assert(n<512 && !found[n]);found[n]=true;
    }
    sk_player_folder_close();assert(!live);
    for(unsigned i=0;i<512;++i) {char name[20];snprintf(name,sizeof(name),"%03u.mp3",i);file(root,name,false);}
    assert(rmdir(root)==0);
    puts("PASS: folder filtering/order, 100 shuffle cycles, current-track preservation, limits and allocation cleanup");
}
