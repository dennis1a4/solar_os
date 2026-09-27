#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nvs.h"
#include "solar_os_storage.h"

static bool mounted=true, fail_sync, fail_replace;
static unsigned replacements;
bool solar_os_storage_flash_is_mounted(void) { return mounted; }
esp_err_t solar_os_storage_sync_file(FILE *file) {
    return !fail_sync && fflush(file)==0 && fsync(fileno(file))==0 ? ESP_OK : ESP_FAIL;
}
esp_err_t sk_settings_replace(const char *a,const char *b) {
    if (fail_replace) return ESP_FAIL;
    ++replacements;
    return rename(a,b)==0 ? ESP_OK : ESP_FAIL;
}
static void expect_value(const char *expected) {
    nvs_handle_t h; char value[64]; size_t len=sizeof(value);
    assert(nvs_open("test",NVS_READONLY,&h)==ESP_OK);
    assert(nvs_get_str(h,"name",value,&len)==ESP_OK);
    assert(!strcmp(value,expected));
    nvs_close(h);
}
int main(void) {
    char dir[]="/tmp/solaros-settings.XXXXXX";
    assert(mkdtemp(dir)); assert(chdir(dir)==0);
    nvs_handle_t h,other;
    assert(nvs_open("../bad",NVS_READWRITE,&h)==ESP_ERR_INVALID_ARG);
    mounted=false;
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_ERR_INVALID_STATE);
    mounted=true;
    assert(nvs_open("test",NVS_READONLY,&h)==ESP_ERR_NOT_FOUND);
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_OK);
    assert(nvs_open("test",NVS_READWRITE,&other)==ESP_ERR_INVALID_STATE);
    assert(nvs_set_str(h,"name","old")==ESP_OK);
    assert(nvs_set_u8(h,"byte",255)==ESP_OK);
    assert(nvs_set_u16(h,"word",0xabcd)==ESP_OK);
    assert(nvs_commit(h)==ESP_OK); nvs_close(h);
    assert(nvs_commit(h)==ESP_ERR_INVALID_STATE);
    assert(nvs_open("test",NVS_READONLY,&h)==ESP_OK);
    uint8_t byte=0; uint16_t word=0;
    assert(nvs_get_u8(h,"byte",&byte)==ESP_OK && byte==255);
    assert(nvs_get_u16(h,"word",&word)==ESP_OK && word==0xabcd);
    assert(nvs_get_u8(h,"word",&byte)==ESP_ERR_INVALID_ARG);
    assert(nvs_get_u8(h,"missing",&byte)==ESP_ERR_NOT_FOUND);
    assert(nvs_set_u8(h,"byte",0)==ESP_ERR_NOT_ALLOWED);
    assert(nvs_commit(h)==ESP_ERR_NOT_ALLOWED);
    size_t len=0;
    assert(nvs_get_str(h,"name",NULL,&len)==ESP_OK && len==4);
    char small[2]={'x','y'}; len=2;
    assert(nvs_get_str(h,"name",small,&len)==ESP_ERR_INVALID_SIZE && len==4 && small[0]=='x');
    nvs_close(h);
    expect_value("old");
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_OK);
    assert(nvs_set_str(h,"name","discarded")==ESP_OK); nvs_close(h);
    expect_value("old");
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_OK);
    assert(nvs_set_str(h,"name","new")==ESP_OK);
    fail_sync=true;
    assert(nvs_commit(h)==ESP_FAIL); fail_sync=false; nvs_close(h);
    expect_value("old");
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_OK);
    assert(nvs_set_str(h,"name","new")==ESP_OK);
    fail_replace=true;
    assert(nvs_commit(h)==ESP_FAIL); fail_replace=false; nvs_close(h);
    expect_value("old");
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_OK);
    assert(nvs_set_str(h,"name","new")==ESP_OK);
    assert(nvs_commit(h)==ESP_OK);
    const unsigned before=replacements;
    assert(nvs_set_str(h,"name","new")==ESP_OK);
    assert(nvs_commit(h)==ESP_OK && replacements==before);
    nvs_close(h); expect_value("new");
    nvs_handle_t slots[4];
    for (unsigned i=0;i<4;i++) { char name[16]; snprintf(name,sizeof(name),"slot%u",i); assert(nvs_open(name,NVS_READWRITE,&slots[i])==ESP_OK); }
    assert(nvs_open("overflow",NVS_READWRITE,&h)==ESP_ERR_NO_MEM);
    for (unsigned i=0;i<4;i++) nvs_close(slots[i]);
    assert(nvs_open("limits",NVS_READWRITE,&h)==ESP_OK);
    char large[65]; memset(large,'x',64); large[64]=0;
    assert(nvs_set_str(h,"large",large)==ESP_ERR_INVALID_SIZE);
    for (unsigned i=0;i<16;i++) { char key[16]; snprintf(key,sizeof(key),"key%u",i); assert(nvs_set_u8(h,key,i)==ESP_OK); }
    assert(nvs_set_u8(h,"overflow",0)==ESP_ERR_NO_MEM);
    nvs_close(h);
    FILE *file=fopen("settings/test.bin","r+b"); assert(file);
    assert(fseek(file,30,SEEK_SET)==0); assert(fputc(99,file)!=EOF); assert(fclose(file)==0);
    assert(nvs_open("test",NVS_READONLY,&h)==ESP_ERR_INVALID_CRC);
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_ERR_INVALID_CRC);
    file=fopen("settings/test.bin","wb"); assert(file); assert(fclose(file)==0);
    assert(nvs_open("test",NVS_READWRITE,&h)==ESP_ERR_INVALID_CRC);
    printf("Settings transaction, bounds, permissions, stale handles, and corruption tests passed (%s)\n",dir);
}
