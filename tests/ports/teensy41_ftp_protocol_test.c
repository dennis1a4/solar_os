/* Shared production client and daemon over real loopback TCP. */
#include <assert.h>
#include <pthread.h>
#include <dirent.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "solar_os_ftp.h"
#include "solar_os_ftpd_job.h"
#include "solar_os_jobs.h"
#include "solar_os_memory.h"
#include "solar_os_task.h"
#include "solar_os_log.h"
struct test_task {pthread_t thread;TaskFunction_t fn;void *arg;};
static struct test_task task;
static unsigned allocations;
size_t strlcpy(char *dst,const char *src,size_t size) {size_t n=strlen(src);if(size){size_t k=n<size-1?n:size-1;memcpy(dst,src,k);dst[k]=0;}return n;}
uint32_t esp_random(void) {static uint32_t token=0;return ++token;}
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t c,const char *tag) {(void)c;(void)tag;void *p=malloc(n);if(p)++allocations;return p;}
void *solar_os_memory_calloc(size_t n,size_t s,solar_os_memory_class_t c,const char *tag) {void *p=solar_os_memory_alloc(n*s,c,tag);if(p)memset(p,0,n*s);return p;}
void solar_os_memory_free(void *p) {if(p){assert(allocations);--allocations;}free(p);}
esp_err_t solar_os_log_write(solar_os_log_level_t level,const char *tag,const char *fmt,...) {(void)level;(void)tag;(void)fmt;return ESP_OK;}
esp_err_t solar_os_net_resolve_host(const char *name,char *out,size_t len) {return strlcpy(out,name,len)<len?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_storage_resolve_path(const char *name,char *out,size_t len) {return strlcpy(out,name,len)<len?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_storage_sync_file(FILE *f) {return fflush(f)==0?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_storage_remove(const char *name) {return remove(name)==0?ESP_OK:ESP_FAIL;}
esp_err_t solar_os_jobs_get_generation(const char *name,uint32_t *out) {(void)name;*out=1;return ESP_OK;}
esp_err_t solar_os_jobs_mark_stopped(const char *name,uint32_t generation,esp_err_t err) {(void)name;(void)generation;return err;}
esp_err_t solar_os_jobs_note_resource(const char *name,solar_os_job_resource_type_t type,const char *resource,const char *detail) {(void)name;(void)type;(void)resource;(void)detail;return ESP_OK;}
void vTaskDelay(TickType_t ms) {usleep(ms*1000);}
static void *run(void *arg) {struct test_task *t=arg;t->fn(t->arg);return NULL;}
BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t fn,const char *name,uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t core,solar_os_task_role_t role) {
    (void)name;(void)bytes;(void)priority;(void)core;(void)role;
    task.fn=fn;task.arg=arg;*out=&task;assert(!pthread_create(&task.thread,NULL,run,&task));return pdPASS;
}
void solar_os_task_delete_internal(TaskHandle_t handle) {(void)handle;pthread_exit(NULL);}
static bool listed;
static bool entry(const solar_os_ftp_entry_t *e,void *arg) {(void)arg;if(!strcmp(e->name,"binary.bin")){assert(e->size==17003);listed=true;}return true;}
static bool cancel(void *arg) {return *(bool *)arg;}
static unsigned free_port(void) {
    int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(bind(fd,(struct sockaddr *)&addr,sizeof(addr))==0);socklen_t size=sizeof(addr);assert(getsockname(fd,(struct sockaddr *)&addr,&size)==0);close(fd);return ntohs(addr.sin_port);
}
static void reply(int fd,int expected,char *line,size_t length) {
    size_t n=0;while(n+1<length){assert(recv(fd,line+n,1,0)==1);if(line[n++]=='\n')break;}line[n]=0;assert(atoi(line)==expected);
}
static void command(int fd,const char *text) {size_t n=strlen(text);assert(send(fd,text,n,0)==(ssize_t)n);}
static int raw_connect(unsigned port) {
    int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    struct timeval timeout={.tv_sec=2};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    assert(connect(fd,(struct sockaddr *)&addr,sizeof(addr))==0);return fd;
}
static void *silent_greeting(void *arg) {
    int listener=*(int *)arg;
    int peer=accept(listener,NULL,NULL);assert(peer>=0);
    struct timeval timeout={.tv_sec=2};
    assert(setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    char byte;
    // A failed handshake must close directly, without issuing another command
    // and waiting for a second reply from a server that never greeted us.
    assert(recv(peer,&byte,1,0)==0);
    close(peer);return NULL;
}
static void greeting_timeout(void) {
    int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(bind(listener,(struct sockaddr *)&addr,sizeof(addr))==0);
    socklen_t size=sizeof(addr);assert(getsockname(listener,(struct sockaddr *)&addr,&size)==0);
    assert(listen(listener,1)==0);pthread_t thread;
    assert(pthread_create(&thread,NULL,silent_greeting,&listener)==0);
    char error[80]={0};
    solar_os_ftp_options_t options={.host="127.0.0.1",.port=ntohs(addr.sin_port),
        .username="anonymous",.password="test",.timeout_ms=150,.error=error,.error_len=sizeof(error)};
    solar_os_ftp_session_t *session=NULL;
    assert(solar_os_ftp_connect(&options,NULL,NULL,&session)==ESP_ERR_TIMEOUT);
    assert(!session && !strcmp(error,"server response timed out"));
    pthread_join(thread,NULL);close(listener);assert(!allocations);
}
static void stalled_stop(const char *root,bool data_connected) {
    char port[8],line[512],saved[192];unsigned number=free_port();snprintf(port,sizeof(port),"%u",number);
    snprintf(saved,sizeof(saved),"%s/kept",root);FILE *f=fopen(saved,"wb");assert(f);fputs("original",f);fclose(f);
    char *args[]={(char *)root,port};assert(solar_os_ftpd_job.start(NULL,2,args)==ESP_OK);
    int control=raw_connect(number);reply(control,220,line,sizeof(line));
    command(control,"USER anonymous\r\n");reply(control,331,line,sizeof(line));
    command(control,"PASS test\r\n");reply(control,230,line,sizeof(line));
    command(control,"PASV\r\n");reply(control,227,line,sizeof(line));
    unsigned a,b,c,d,hi,lo;assert(sscanf(strchr(line,'('),"(%u,%u,%u,%u,%u,%u)",&a,&b,&c,&d,&hi,&lo)==6);
    int data=data_connected?raw_connect(hi*256+lo):-1;
    command(control,"STOR /kept\r\n");reply(control,150,line,sizeof(line));
    if(data>=0)command(data,"partial upload");
    usleep(30000);
    struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
    solar_os_ftpd_job.stop(NULL);pthread_join(task.thread,NULL);clock_gettime(CLOCK_MONOTONIC,&end);
    assert((end.tv_sec-start.tv_sec)*1000000000LL+end.tv_nsec-start.tv_nsec<2000000000LL);
    close(control);if(data>=0)close(data);
    f=fopen(saved,"rb");assert(f);assert(fread(line,1,8,f)==8 && !memcmp(line,"original",8));fclose(f);assert(!remove(saved));
    DIR *dir=opendir(root);assert(dir);struct dirent *entry;
    while((entry=readdir(dir)))assert(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."));
    closedir(dir);
}
int main(int argc,char **argv) {
    greeting_timeout();
    assert(argc==2);char source[160],dest[160],root[160];
    snprintf(source,sizeof(source),"%s/source",argv[1]);snprintf(dest,sizeof(dest),"%s/result",argv[1]);snprintf(root,sizeof(root),"%s/export",argv[1]);assert(!mkdir(root,0700));
    FILE *file=fopen(source,"wb");assert(file);for(unsigned i=0;i<17003;++i)fputc(i&255,file);assert(!fclose(file));
    for(unsigned cycle=0;cycle<5;++cycle) {
        char port[8];snprintf(port,sizeof(port),"%u",free_port());char *args[]={root,port,"--user","test","--password","secret"};
        assert(solar_os_ftpd_job.start(NULL,6,args)==ESP_OK);
        solar_os_ftp_options_t options={.host="127.0.0.1",.port=atoi(port),.username="test",.password="wrong",.timeout_ms=500};
        solar_os_ftp_session_t *session=NULL;bool cancelled=false;
        assert(solar_os_ftp_connect(&options,cancel,&cancelled,&session)!=ESP_OK && !session);
        options.password="secret";assert(solar_os_ftp_connect(&options,cancel,&cancelled,&session)==ESP_OK);
        assert(solar_os_ftp_mkdir(session,"/folder")==ESP_OK);
        assert(solar_os_ftp_upload(session,source,"/folder/binary.bin",NULL,NULL)==ESP_OK);
        listed=false;assert(solar_os_ftp_list(session,"/folder",entry,NULL)==ESP_OK && listed);
        assert(solar_os_ftp_download(session,"/folder/binary.bin",dest,NULL,NULL)==ESP_OK);
        file=fopen(dest,"rb");assert(file);for(unsigned i=0;i<17003;++i)assert(fgetc(file)==(int)(i&255));assert(fgetc(file)==EOF);fclose(file);
        assert(solar_os_ftp_download(session,"/missing",dest,NULL,NULL)!=ESP_OK);
        file=fopen(dest,"rb");assert(file);assert(fgetc(file)==0 && fgetc(file)==1);fclose(file);
        assert(solar_os_ftp_rename(session,"/folder/binary.bin","/folder/renamed.bin")==ESP_OK);
        assert(solar_os_ftp_remove(session,"/folder/renamed.bin")==ESP_OK);
        assert(solar_os_ftp_rmdir(session,"/folder")==ESP_OK);
        assert(solar_os_ftp_remove(session,"/../../source")!=ESP_OK);
        assert(solar_os_ftp_mkdir(session,"bad\r\nQUIT")!=ESP_OK);
        cancelled=true;assert(solar_os_ftp_list(session,"/",entry,NULL)!=ESP_OK);
        solar_os_ftp_disconnect(session);solar_os_ftpd_job.stop(NULL);pthread_join(task.thread,NULL);assert(!allocations);
    }
    stalled_stop(root,true);stalled_stop(root,false);
    assert(!remove(source));assert(!remove(dest));assert(!rmdir(root));puts("FTP shared client/server: PASS (5 binary round trips, auth, listing, mutations, preservation, traversal, cancellation)");
}
