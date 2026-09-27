#if SK_SETTINGS
/* Bounded NVS compatibility for the single console task. Each namespace is a
 * checksummed snapshot. Commit syncs a temporary file then atomically replaces
 * the previous LittleFS file; failed/abandoned writes leave it untouched.
 * Namespace/key names: 1..15 ASCII identifier characters; values: u8/u16 or
 * strings up to 63 bytes. Handles own staged data until close. No SD fallback.
 */
#include "nvs.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "solar_os_storage.h"

#ifndef SK_SETTINGS_DIR
#define SK_SETTINGS_DIR "/flash/.solar-settings"
#endif
#define ENTRY_SIZE 82U
#define ENTRY_COUNT 16U
#define IMAGE_SIZE (12U + ENTRY_COUNT * ENTRY_SIZE)
#define HANDLE_COUNT 4U
typedef struct {
    unsigned id;
    bool writable, dirty;
    char name[16];
    unsigned char image[IMAGE_SIZE];
} settings_handle_t;
static settings_handle_t *handles[HANDLE_COUNT];
static unsigned next_id;
/* Port implementation permits replacement only inside the settings directory. */
extern esp_err_t sk_settings_replace(const char *source, const char *dest);

static bool valid_name(const char *s) {
    if (!s || !*s || strlen(s)>15) return false;
    for (; *s; ++s) if (!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') ||
        (*s>='0' && *s<='9') || *s=='_' || *s=='-')) return false;
    return true;
}
static settings_handle_t *lookup(nvs_handle_t id) {
    for (unsigned i=0;i<HANDLE_COUNT;i++) if (handles[i] && handles[i]->id==id) return handles[i];
    return NULL;
}
static uint32_t checksum(const unsigned char *p, size_t n) {
    uint32_t crc=0xffffffffU;
    while (n--) {
        crc ^= *p++;
        for (unsigned i=0;i<8;i++) crc=(crc>>1) ^ (0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}
static void path_for(const settings_handle_t *h, char *path, size_t len, bool temp) {
    snprintf(path,len,SK_SETTINGS_DIR "/%s.%s",h->name,temp ? "tmp" : "bin");
}
static bool valid_image(const unsigned char *image) {
    if (memcmp(image,"SKNVS001",8)) return false;
    uint32_t stored=0;
    for (unsigned i=0;i<4;i++) stored |= (uint32_t)image[8+i] << (8*i);
    if (stored!=checksum(image+12,IMAGE_SIZE-12)) return false;
    for (unsigned i=0;i<ENTRY_COUNT;i++) {
        const unsigned char *e=image+12+i*ENTRY_SIZE;
        if (!e[0]) {
            for (unsigned j=0;j<ENTRY_SIZE;j++) if (e[j]) return false;
            continue;
        }
        if (!memchr(e,0,16) || !valid_name((const char *)e)) return false;
        if (!((e[16]==1 && e[17]==1) || (e[16]==2 && e[17]==2) ||
              (e[16]==3 && e[17]>=1 && e[17]<=64 && e[18+e[17]-1]==0 &&
               strlen((const char *)e+18)==(size_t)e[17]-1))) return false;
        for (unsigned j=0;j<i;j++) if (!strcmp((const char *)e,(const char *)image+12+j*ENTRY_SIZE)) return false;
    }
    return true;
}
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) {
    if (!out || !valid_name(name) || (mode!=NVS_READONLY && mode!=NVS_READWRITE)) return ESP_ERR_INVALID_ARG;
    *out=0;
    if (!solar_os_storage_flash_is_mounted()) return ESP_ERR_INVALID_STATE;
    unsigned slot=HANDLE_COUNT;
    for (unsigned i=0;i<HANDLE_COUNT;i++) {
        if (!handles[i]) slot=i;
        else if (!strcmp(handles[i]->name,name)) return ESP_ERR_INVALID_STATE;
    }
    if (slot==HANDLE_COUNT) return ESP_ERR_NO_MEM;
    settings_handle_t *h=calloc(1,sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    strcpy(h->name,name); h->writable=mode==NVS_READWRITE;
    char path[96]; path_for(h,path,sizeof(path),false);
    FILE *file=fopen(path,"rb");
    esp_err_t err=ESP_OK;
    if (file) {
        const size_t count=fread(h->image,1,IMAGE_SIZE,file);
        const int extra=fgetc(file);
        if (count!=IMAGE_SIZE || extra!=EOF || ferror(file) || !valid_image(h->image)) err=ESP_ERR_INVALID_CRC;
        if (fclose(file) && err==ESP_OK) err=ESP_FAIL;
    } else if (errno!=ENOENT) err=ESP_FAIL;
    else if (!h->writable) err=ESP_ERR_NOT_FOUND;
    else memcpy(h->image,"SKNVS001",8);
    if (err!=ESP_OK) { free(h); return err; }
    do { ++next_id; } while (!next_id || lookup(next_id));
    h->id=next_id; handles[slot]=h; *out=h->id;
    return ESP_OK;
}
void nvs_close(nvs_handle_t id) {
    for (unsigned i=0;i<HANDLE_COUNT;i++) if (handles[i] && handles[i]->id==id) {
        free(handles[i]); handles[i]=NULL; return;
    }
}
static esp_err_t entry(nvs_handle_t id,const char *key,bool write,unsigned char **out) {
    settings_handle_t *h=lookup(id);
    if (!h) return ESP_ERR_INVALID_STATE;
    if (!valid_name(key)) return ESP_ERR_INVALID_ARG;
    if (write && !h->writable) return ESP_ERR_NOT_ALLOWED;
    unsigned char *empty=NULL;
    for (unsigned i=0;i<ENTRY_COUNT;i++) {
        unsigned char *e=h->image+12+i*ENTRY_SIZE;
        if (!strcmp((char *)e,key)) { *out=e; return ESP_OK; }
        if (!e[0]) empty=e;
    }
    if (!write) return ESP_ERR_NOT_FOUND;
    if (!empty) return ESP_ERR_NO_MEM;
    *out=empty; return ESP_OK;
}
static esp_err_t set(nvs_handle_t id,const char *key,unsigned type,const void *value,size_t len) {
    unsigned char *e;
    esp_err_t err=entry(id,key,true,&e);
    if (err!=ESP_OK) return err;
    if (e[16]==type && e[17]==len && !memcmp(e+18,value,len)) return ESP_OK;
    memset(e,0,ENTRY_SIZE); strcpy((char *)e,key); e[16]=type; e[17]=len;
    memcpy(e+18,value,len); lookup(id)->dirty=true; return ESP_OK;
}
static esp_err_t get(nvs_handle_t id,const char *key,unsigned type,void *value,size_t *len) {
    if (!len) return ESP_ERR_INVALID_ARG;
    unsigned char *e;
    esp_err_t err=entry(id,key,false,&e);
    if (err!=ESP_OK) return err;
    if (e[16]!=type) return ESP_ERR_INVALID_ARG;
    const size_t available=*len; *len=e[17];
    if (!value) return ESP_OK;
    if (available<*len) return ESP_ERR_INVALID_SIZE;
    memcpy(value,e+18,*len); return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) { return set(h,k,1,&v,1); }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) { size_t n=1; return v ? get(h,k,1,v,&n) : ESP_ERR_INVALID_ARG; }
esp_err_t nvs_set_u16(nvs_handle_t h,const char *k,uint16_t v) { unsigned char b[2]={v&255,v>>8}; return set(h,k,2,b,2); }
esp_err_t nvs_get_u16(nvs_handle_t h,const char *k,uint16_t *v) {
    if (!v) return ESP_ERR_INVALID_ARG;
    unsigned char b[2]; size_t n=2; esp_err_t err=get(h,k,2,b,&n);
    if (err==ESP_OK) *v=b[0] | ((uint16_t)b[1]<<8);
    return err;
}
esp_err_t nvs_set_str(nvs_handle_t h,const char *k,const char *v) {
    if (!v) return ESP_ERR_INVALID_ARG;
    if (strlen(v)>63) return ESP_ERR_INVALID_SIZE;
    return set(h,k,3,v,strlen(v)+1);
}
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *v,size_t *n) { return get(h,k,3,v,n); }
esp_err_t nvs_commit(nvs_handle_t id) {
    settings_handle_t *h=lookup(id);
    if (!h) return ESP_ERR_INVALID_STATE;
    if (!h->writable) return ESP_ERR_NOT_ALLOWED;
    if (!h->dirty) return ESP_OK;
    if (!solar_os_storage_flash_is_mounted()) return ESP_ERR_INVALID_STATE;
    if (mkdir(SK_SETTINGS_DIR,0700) && errno!=EEXIST) return ESP_FAIL;
    uint32_t crc=checksum(h->image+12,IMAGE_SIZE-12);
    for (unsigned i=0;i<4;i++) h->image[8+i]=(crc>>(8*i))&255;
    char path[96],temp[96]; path_for(h,path,sizeof(path),false); path_for(h,temp,sizeof(temp),true);
    FILE *file=fopen(temp,"wb");
    if (!file) return ESP_FAIL;
    esp_err_t err=fwrite(h->image,1,IMAGE_SIZE,file)==IMAGE_SIZE ? ESP_OK : ESP_FAIL;
    if (err==ESP_OK) err=solar_os_storage_sync_file(file);
    if (fclose(file) && err==ESP_OK) err=ESP_FAIL;
    if (err==ESP_OK) err=sk_settings_replace(temp,path);
    if (err==ESP_OK) h->dirty=false;
    else remove(temp);
    return err;
}
#endif
