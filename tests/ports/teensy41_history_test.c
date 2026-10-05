#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#define SHELL_HISTORY_LEN 12
#define SHELL_INPUT_MAX 192
#define ESP_OK 0
#define pdTICKS_TO_MS(x) (x)
static uint32_t now;
static unsigned writes;
static bool fail_replace, fail_open, fail_close;
static uint32_t xTaskGetTickCount(void) { return now; }
static size_t strlcpy(char *d,const char *s,size_t n) {
    size_t len=strlen(s); if(n) {size_t k=len<n-1?len:n-1;memcpy(d,s,k);d[k]=0;} return len;
}
typedef struct {
    char history[SHELL_HISTORY_LEN][SHELL_INPUT_MAX];
    size_t history_count;
    unsigned history_store;
    bool history_dirty;
    uint32_t history_saved_ms;
} solar_os_shell_session_t;
static int solar_os_storage_mkdir(const char *path) { return mkdir(path+7,0700); }
static FILE *history_fopen(const char *path,const char *mode) {
    if (*mode=='w') { ++writes;if(fail_open)return NULL; }
    return fopen(path+7,mode);
}
static int history_fclose(FILE *f) { int result=fclose(f);return fail_close?EOF:result; }
int sk_history_replace(const char *source,const char *dest) {
    return fail_replace ? -1 : rename(source+7,dest+7);
}
#include "history_add.inc"
#define fopen history_fopen
#define fclose history_fclose
#include "shell_history.h"
#undef fopen
#undef fclose
static void add(solar_os_shell_session_t *s,const char *line) {
    if(shell_history_add_ram(s,line))s->history_dirty=true;
}
static solar_os_shell_session_t load(unsigned id) {
    solar_os_shell_session_t s={0};solar_os_shell_history_store(&s,id);shell_history_load(&s);return s;
}
int main(void) {
    char dir[]="/tmp/solaros-history.XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    solar_os_shell_session_t s=load(1);assert(!s.history_count);
    add(&s,"echo first");now=29999;assert(solar_os_shell_history_flush(&s,false));assert(!writes);
    now=30000;assert(solar_os_shell_history_flush(&s,false));assert(writes==1 && !s.history_dirty);
    assert(!strcmp(load(1).history[0],"echo first"));assert(!load(2).history_count);
    add(&s,"echo first");assert(!s.history_dirty);
    add(&s,"echo second");fail_replace=true;assert(!solar_os_shell_history_flush(&s,true));
    assert(s.history_dirty && load(1).history_count==1);
    fail_replace=false;fail_open=true;assert(!solar_os_shell_history_flush(&s,true));
    fail_open=false;fail_close=true;assert(!solar_os_shell_history_flush(&s,true));
    fail_close=false;assert(solar_os_shell_history_flush(&s,true));assert(load(1).history_count==2);
    for(unsigned i=0;i<30;++i) {char b[32];snprintf(b,sizeof(b),"echo %u",i);add(&s,b);}
    assert(s.history_count==12);assert(solar_os_shell_history_flush(&s,true));
    solar_os_shell_session_t r=load(1);assert(r.history_count==12);assert(!strcmp(r.history[0],"echo 18"));
    solar_os_shell_session_t usb=load(2);add(&usb,"echo usb");assert(solar_os_shell_history_flush(&usb,true));
    assert(!strcmp(load(1).history[11],"echo 29"));assert(!strcmp(load(2).history[0],"echo usb"));
    FILE *f=fopen(".shell/history-lcd","w");assert(f);fputs("echo good\necho incomplete",f);fclose(f);
    r=load(1);assert(r.history_count==1 && !strcmp(r.history[0],"echo good"));
    f=fopen(".shell/history-lcd","w");assert(f);for(unsigned i=0;i<500;++i)fputc('x',f);fputs("\necho bad\n",f);fclose(f);
    assert(load(1).history_count==0);
    s.history_saved_ms=UINT32_MAX-10000;now=20000;add(&s,"echo wrap");assert(solar_os_shell_history_flush(&s,false));assert(!s.history_dirty);
    assert(!load(0).history_count);
    puts("PASS: history batching, atomic replacement failures, reload, limits, console isolation, malformed records and timer wrap");
}
