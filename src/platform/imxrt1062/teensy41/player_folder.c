#if SK_WEBRADIO
// Teensy backing store for the upstream player. The playlist API is a transient
// folder view: no flash writes, saved playlist, or per-track file handles.
#include "player_folder.h"
#include "solar_os_memory.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
extern uint32_t sk_python_random_seed(void);
static solar_os_player_track_t *tracks;
static uint16_t *order;
static size_t count, capacity;
static uint32_t generation, rng;
static bool shuffled;
static uint32_t random_word(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static unsigned random_below(unsigned n) {
    const uint32_t limit = UINT32_MAX - UINT32_MAX % n;
    uint32_t value;
    do { value=random_word(); } while (value >= limit);
    return value % n;
}
void sk_player_folder_close(void) {
    solar_os_memory_free(tracks); solar_os_memory_free(order);
    tracks=NULL; order=NULL; count=capacity=0; shuffled=false; ++generation;
}
static int compare_tracks(const void *a, const void *b) {
    const char *x=((const solar_os_player_track_t *)a)->path;
    const char *y=((const solar_os_player_track_t *)b)->path;
    int result=strcasecmp(x,y); return result ? result : strcmp(x,y);
}
esp_err_t sk_player_folder_open(const char *path) {
    sk_player_folder_close();
    DIR *dir=opendir(path);
    if (!dir) return ESP_ERR_NOT_FOUND;
    esp_err_t err=ESP_OK;
    struct dirent *entry;
    while ((entry=readdir(dir)) != NULL) {
        if (entry->d_name[0]=='.') continue;
        const char *dot=strrchr(entry->d_name,'.');
        if (!dot || (strcasecmp(dot,".mp3") && strcasecmp(dot,".wav"))) continue;
        solar_os_player_track_t track;
        err=solar_os_storage_join_path(path,entry->d_name,track.path,sizeof(track.path));
        if (err!=ESP_OK) break;
        struct stat st;
        if (stat(track.path,&st)!=0) { err=ESP_FAIL; break; }
        if (!S_ISREG(st.st_mode)) continue;
        if (count==SK_PLAYER_FOLDER_MAX) { err=ESP_ERR_INVALID_SIZE; break; }
        if (count==capacity) {
            size_t next=capacity ? capacity*2 : 16;
            solar_os_player_track_t *grown=solar_os_memory_alloc(next*sizeof(*tracks),
                SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"player.folder");
            if (!grown) { err=ESP_ERR_NO_MEM; break; }
            if (count) memcpy(grown,tracks,count*sizeof(*tracks));
            solar_os_memory_free(tracks); tracks=grown; capacity=next;
        }
        tracks[count++]=track;
    }
    if (closedir(dir)!=0 && err==ESP_OK) err=ESP_FAIL;
    if (err==ESP_OK && !count) err=ESP_ERR_NOT_FOUND;
    if (err==ESP_OK) {
        order=solar_os_memory_alloc(count*sizeof(*order),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"player.order");
        if (!order) err=ESP_ERR_NO_MEM;
    }
    if (err!=ESP_OK) { sk_player_folder_close(); return err; }
    qsort(tracks,count,sizeof(*tracks),compare_tracks);
    for (size_t i=0;i<count;++i) order[i]=i;
    rng=sk_python_random_seed(); if (!rng) rng=0x6d2b79f5U;
    ++generation;
    return ESP_OK;
}
// Preserve the currently playing track when toggling order. SIZE_MAX requests
// a fresh cycle. The app handles repeat policy; this service only orders tracks.
size_t sk_player_folder_shuffle(bool enabled, size_t keep_index) {
    if (!count) return 0;
    uint16_t keep=keep_index<count ? order[keep_index] : UINT16_MAX;
    for (size_t i=0;i<count;++i) order[i]=i;
    if (enabled) {
        for (size_t i=count-1;i>0;--i) {
            size_t j=random_below(i+1); uint16_t t=order[i]; order[i]=order[j]; order[j]=t;
        }
        if (keep!=UINT16_MAX) {
            for (size_t i=0;i<count;++i) if (order[i]==keep) {
                order[i]=order[0]; order[0]=keep; break;
            }
        }
    }
    shuffled=enabled; ++generation;
    if (!enabled && keep!=UINT16_MAX) return keep;
    return 0;
}
bool sk_player_folder_shuffled(void) { return shuffled; }
esp_err_t solar_os_player_playlist_init(void) { return ESP_OK; }
size_t solar_os_player_playlist_count(void) { return count; }
uint32_t solar_os_player_playlist_generation(void) { return generation; }
bool solar_os_player_playlist_get(size_t index, solar_os_player_track_t *track) {
    if (!track || index>=count) return false;
    *track=tracks[order[index]]; return true;
}
// Folder membership is read-only while open; reopen to rescan changed files.
esp_err_t solar_os_player_playlist_add(const char *path,size_t *index) {
    (void)path; (void)index; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t solar_os_player_playlist_remove(size_t index) {
    (void)index; return ESP_ERR_NOT_SUPPORTED;
}
#endif
