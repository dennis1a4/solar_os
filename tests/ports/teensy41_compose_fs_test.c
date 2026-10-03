#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_shell_commands.h"
#include "solar_os_shell_common.h"
#include "solar_os_shell_io.h"
#include "solar_os_storage.h"
static solar_os_shell_io_t io;
static solar_os_context_t ctx;
static size_t written;
size_t strlcpy(char *d,const char *s,size_t n){size_t k=strlen(s);if(n){size_t m=k<n-1?k:n-1;memcpy(d,s,m);d[m]=0;}return k;}
solar_os_shell_io_t *solar_os_shell_context_io(solar_os_context_t *c){(void)c;return &io;}
esp_err_t solar_os_shell_resolve_path(solar_os_context_t *c,const char *p,char *out,size_t n){(void)c;return strlcpy(out,p?p:".",n)<n?ESP_OK:ESP_ERR_INVALID_SIZE;}
bool solar_os_shell_resolve_path_for_command(solar_os_context_t *c,solar_os_shell_io_t *s,const char *cmd,const char *p,char *out,size_t n){(void)cmd;if(solar_os_shell_resolve_path(c,p,out,n)==ESP_OK)return true;s->command_status=1;return false;}
esp_err_t solar_os_shell_set_cwd(solar_os_context_t *c,const char *p){(void)c;return chdir(p)?ESP_FAIL:ESP_OK;}
esp_err_t solar_os_storage_mkdir(const char *p){return mkdir(p,0700)?ESP_FAIL:ESP_OK;}
esp_err_t solar_os_storage_remove(const char *p){return unlink(p)?ESP_FAIL:ESP_OK;}
esp_err_t solar_os_storage_rmdir(const char *p){return rmdir(p)?ESP_FAIL:ESP_OK;}
esp_err_t solar_os_storage_rename(const char *a,const char *b){return rename(a,b)?ESP_FAIL:ESP_OK;}
esp_err_t solar_os_storage_copy_file(const char *a,const char *b){(void)a;(void)b;errno=EIO;return ESP_FAIL;}
const char *solar_os_storage_mount_point(void){return "/";}
esp_err_t solar_os_storage_path_mount_point(const char *p,char *out,size_t n){(void)p;strlcpy(out,"/",n);return ESP_OK;}
esp_err_t solar_os_shell_io_write_len(solar_os_shell_io_t *s,const char *p,size_t n){(void)p;written+=n;if(s->command_output_fn){esp_err_t e=s->command_output_fn(p,n,s->command_output_user);if(e!=ESP_OK)s->command_status=1;return e;}return ESP_OK;}
esp_err_t solar_os_shell_io_write(solar_os_shell_io_t *s,const char *p){return solar_os_shell_io_write_len(s,p,strlen(p));}
esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *s,const char *p,...){(void)s;(void)p;return ESP_OK;}
esp_err_t solar_os_shell_io_write_bold(solar_os_shell_io_t *s,const char *p){return solar_os_shell_io_write(s,p);}
esp_err_t solar_os_shell_io_put_char(solar_os_shell_io_t *s,char c){return solar_os_shell_io_write_len(s,&c,1);}
esp_err_t solar_os_shell_io_newline(solar_os_shell_io_t *s){return solar_os_shell_io_put_char(s,'\n');}
void solar_os_shell_diag_missing(solar_os_shell_io_t *s,const char *c,const char *a,const char *u){(void)c;(void)a;(void)u;s->command_status=1;}
void solar_os_shell_diag_unexpected(solar_os_shell_io_t *s,const char *c,const char *a,const char *u){solar_os_shell_diag_missing(s,c,a,u);}
void solar_os_shell_diag_invalid(solar_os_shell_io_t *s,const char *c,const char *a,const char *v,const char *e,const char *u,bool b){(void)v;(void)e;(void)b;solar_os_shell_diag_missing(s,c,a,u);}
static esp_err_t sink(const char *p,size_t n,void *u){(void)p;(void)n;return u?ESP_ERR_NO_MEM:ESP_OK;}
static void call(void (*fn)(solar_os_context_t *,int,char **),int n,char **args,int expected){io.command_status=-1;fn(&ctx,n,args);assert(io.command_status==expected);}
#define RUN(fn, expected, ...) do {char *args[]={__VA_ARGS__};call(solar_os_shell_cmd_##fn,sizeof(args)/sizeof(args[0]),args,expected);} while(0)
int main(void){
    char root[]="/tmp/solaros-compose-fs.XXXXXX";assert(mkdtemp(root));assert(!chdir(root));
    RUN(cd,1,"cd","missing");RUN(cd,0,"cd",".");RUN(cd,1,"cd","a","b");
    RUN(mkdir,0,"mkdir","dir");RUN(mkdir,1,"mkdir","dir","dir2");
    RUN(ls,0,"ls",".");RUN(ls,1,"ls","missing");RUN(ls,1,"ls","none*");RUN(ls,1,"ls","-z");
    RUN(cat,1,"cat","missing");RUN(cat,1,"cat","none*");
    FILE *f=fopen("file","wb");assert(f);for(int i=0;i<6000;++i)assert(fputc('x',f)!=EOF);assert(!fclose(f));
    io.command_output_fn=sink;written=0;RUN(cat,0,"cat","file");assert(written==6000);
    io.command_output_user=&io;RUN(cat,1,"cat","file");io.command_output_user=NULL;io.command_output_fn=NULL;
    RUN(cp,1,"cp","file","copy");RUN(mv,0,"mv","file","moved");RUN(mv,1,"mv","missing","other");
    RUN(rm,1,"rm","missing");RUN(rm,0,"rm","-f","missing");RUN(rm,1,"rm","dir");RUN(rm,0,"rm","-rf","dir");
    RUN(rm,1,"rm","moved","missing");RUN(rm,1,"rm","none*");RUN(rm,0,"rm","-f","none*");
    puts("PASS: actual filesystem command exit statuses and byte-exact pipe cat");
}
