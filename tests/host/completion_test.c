#include "solar_os_completion.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *const *names;
static bool complete(const solar_os_completion_request_t *r,solar_os_completion_emit_t emit,void *sink,void *user) {
    (void)r;(void)user;
    for(size_t i=0;names[i];++i)if(!emit(sink,names[i]))return false;
    return true;
}
static bool failed(const solar_os_completion_request_t *r,solar_os_completion_emit_t emit,void *sink,void *user) {
    (void)r;(void)user;emit(sink,"edit");return false;
}
static bool listed(void *user,const char *name) {++*(size_t *)user;assert(name);return true;}
int main(void) {
    solar_os_completion_registry_t registry={0};
    registry.providers[SOLAR_OS_COMPLETE_CUSTOM].enumerate=complete;
    char line[512];size_t cursor;solar_os_completion_result_t r;
    const char *commands[]={"edit",NULL};names=commands;
#define APPLY() solar_os_completion_apply(line,sizeof(line),&cursor,SOLAR_OS_COMPLETE_CUSTOM,&registry,false,NULL,NULL)
    strcpy(line,"ed /sd/a");cursor=2;r=APPLY();assert(r.changed && !strcmp(line,"edit /sd/a") && cursor==4);
    strcpy(line,"edZZ /sd/a");cursor=2;r=APPLY();assert(r.changed && !strcmp(line,"edit /sd/a"));
    strcpy(line,"  ed /sd/a");cursor=4;r=APPLY();assert(r.changed && !strcmp(line,"  edit /sd/a"));
    const char *paths[]={"/sd/my notes/",NULL};names=paths;
    strcpy(line,"edit \"/sd/my no\" next");cursor=15;r=APPLY();assert(r.changed && !strcmp(line,"edit \"/sd/my notes/\" next"));assert(line[cursor]=='"');
    strcpy(line,"edit /sd/my\\ no");cursor=strlen(line);r=APPLY();assert(r.changed && !strcmp(line,"edit \"/sd/my notes/\""));
    strcpy(line,"edit '/sd/my no");cursor=strlen(line);r=APPLY();assert(r.changed && !strcmp(line,"edit \"/sd/my notes/\""));
    strcpy(line,"/sd/my no");cursor=strlen(line);
    r=solar_os_completion_apply_field(line,sizeof(line),&cursor,SOLAR_OS_COMPLETE_CUSTOM,&registry,false,NULL,NULL);
    assert(r.changed && !strcmp(line,"/sd/my notes/") && cursor==strlen(line));
    const char *many[]={"alpha","alpine",NULL};names=many;
    strcpy(line,"a");cursor=1;r=APPLY();assert(r.matches==2 && r.changed && !strcmp(line,"alp"));
    size_t count=0;r=solar_os_completion_apply(line,sizeof(line),&cursor,SOLAR_OS_COMPLETE_CUSTOM,&registry,true,listed,&count);
    assert(!r.changed && count==2 && r.displayed==2);
    names=commands;strcpy(line,"ed");cursor=2;r=solar_os_completion_apply(line,4,&cursor,SOLAR_OS_COMPLETE_CUSTOM,&registry,false,NULL,NULL);
    assert(r.overflow && !strcmp(line,"ed") && cursor==2);
    registry.providers[SOLAR_OS_COMPLETE_CUSTOM].enumerate=failed;r=APPLY();assert(r.failed && !strcmp(line,"ed"));
    char directory[]="/tmp/solaros-completion-XXXXXX";assert(mkdtemp(directory));
    char path[512];snprintf(path,sizeof(path),"%s/my notes",directory);assert(!mkdir(path,0700));
    snprintf(path,sizeof(path),"%s/my file",directory);FILE *f=fopen(path,"w");assert(f);fclose(f);
    solar_os_completion_files_t fs={.cwd=directory};registry.providers[SOLAR_OS_COMPLETE_FILE]=(solar_os_completion_provider_entry_t){solar_os_completion_files,&fs};
    registry.providers[SOLAR_OS_COMPLETE_DIRECTORY]=registry.providers[SOLAR_OS_COMPLETE_FILE];
    strcpy(line,"edit my");cursor=strlen(line);r=solar_os_completion_apply(line,sizeof(line),&cursor,SOLAR_OS_COMPLETE_FILE,&registry,false,NULL,NULL);
    assert(r.matches==2 && !strcmp(line,"edit \"my \""));
    strcpy(line,"cd my");cursor=strlen(line);r=solar_os_completion_apply(line,sizeof(line),&cursor,SOLAR_OS_COMPLETE_DIRECTORY,&registry,false,NULL,NULL);
    assert(r.matches==1 && !strcmp(line,"cd \"my notes/\""));
    for(unsigned i=0;i<45;++i){snprintf(path,sizeof(path),"%s/file%02u",directory,i);f=fopen(path,"w");assert(f);fclose(f);}
    strcpy(line,"cat file");cursor=strlen(line);count=0;r=solar_os_completion_apply(line,sizeof(line),&cursor,SOLAR_OS_COMPLETE_FILE,&registry,true,listed,&count);
    assert(r.matches==45 && r.displayed==20 && count==20 && !r.failed);
    snprintf(path,sizeof(path),"%s/my file",directory);unlink(path);
    snprintf(path,sizeof(path),"%s/my notes",directory);rmdir(path);
    for(unsigned i=0;i<45;++i){snprintf(path,sizeof(path),"%s/file%02u",directory,i);unlink(path);}rmdir(directory);
    puts("PASS: cursor token replacement, suffix preservation, quoting, LCP, repeat Tab, capacity/error atomicity, file/directory filtering, bounded listing");
}
