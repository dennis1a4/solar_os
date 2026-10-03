#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include "platform/imxrt1062/teensy41/shell_plan.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_shell_parse.h"
#include "solar_os_memory.h"
int sk_shell_compose(solar_os_context_t *,const char *);
void sk_shell_pipe_release(solar_os_shell_io_t *);
extern const solar_os_app_t solar_os_less_app = {};
void sk_shell_cmd_filter(solar_os_context_t *,int,char **);
}
static solar_os_shell_io_t io;
static solar_os_context_t context;
static std::string output;
static std::vector<std::string> calls;
static int allocation_count, fail_after=-1, polls, cancel_at=-1;
extern "C" solar_os_shell_io_t *solar_os_context_shell_io(solar_os_context_t *) {return &io;}
extern "C" bool sk_console_poll_cancel(bool) {return ++polls==cancel_at;}
extern "C" void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t kind,const char *) {
    assert(kind==SOLAR_OS_MEMORY_EXTERNAL_SYSTEM);
    if(fail_after==0)return nullptr;
    if(fail_after>0)--fail_after;
    ++allocation_count;return malloc(n);
}
extern "C" void solar_os_memory_free(void *p){if(p){--allocation_count;free(p);}}
extern "C" bool solar_os_port_handle_valid(const solar_os_port_handle_t *) {return true;}
extern "C" esp_err_t solar_os_port_write(const solar_os_port_handle_t *,const uint8_t *p,size_t n,size_t *written) {
    *written=n;for(size_t i=0;i<n;++i)if(p[i]!='\r')output+=char(p[i]);return ESP_OK;
}
extern "C" esp_err_t solar_os_shell_execute_command(solar_os_context_t *ctx,const char *line) {
    calls.push_back(line);char text[192];strcpy(text,line);char *argv[20];
    auto r=solar_os_shell_tokenize(text,argv,20);assert(r.error==SOLAR_OS_SHELL_PARSE_OK);
    if(!strcmp(argv[0],"grep") || !strcmp(argv[0],"head") || !strcmp(argv[0],"wc"))sk_shell_cmd_filter(ctx,r.argc,argv);
    else if(!strcmp(argv[0],"ntp")){io.command_status=1;}
    else if(!strcmp(argv[0],"echo")) {
        io.command_status=0;
        for(int i=1;i<r.argc;++i){if(i>1)solar_os_shell_io_write_len(&io," ",1);solar_os_shell_io_write_len(&io,argv[i],strlen(argv[i]));}
        solar_os_shell_io_write_len(&io,"\n",1);
    } else if(!strcmp(argv[0],"cat")) {
        io.command_status=0;
        if(r.argc==1)solar_os_shell_io_write_len(&io,io.command_input,io.command_input_size);
        else {
            const std::string data=!strcmp(argv[1],"big")?std::string(8193,'x'):!strcmp(argv[1],"exact")?std::string(8192,'x'):"one\ntwo words\nlast";
            solar_os_shell_io_write_len(&io,data.data(),data.size());
        }
    } else if(!strcmp(argv[0],"less")) {ctx->requested_app=&solar_os_less_app;} else io.command_status=0;
    return ESP_OK;
}
static void run(const char *line) {
    io={};io.kind=SOLAR_OS_SHELL_IO_KIND_PORT;io.terminal_profile=SOLAR_OS_SHELL_TERMINAL_PROFILE_DUMB;output.clear();calls.clear();polls=0;
    context.requested_app=nullptr;
    const int result=sk_shell_compose(&context,line);
    if(context.requested_app) {
        assert(result==0 && io.command_input && allocation_count==1);
        output.assign(io.command_input,io.command_input_size);
        sk_shell_pipe_release(&io);
    } else assert(result==1);
    assert(allocation_count==0 && !io.command_output_fn && !io.command_input);
}
int main() {
    assert(!skshell::plan("echo 'a;b&&c|d'").composed);
    assert(!skshell::plan("echo a\\;b").composed);
    assert(skshell::plan("echo x&&echo y;cat x|wc").count==4);
    for(auto s:{";echo x","echo x;;echo y","echo x&&","echo x|","echo x||echo y","echo x>f","echo 'x","echo x\\"})assert(skshell::plan(s).error);
    assert(skshell::plan("echo x;").count==1);
    assert(skshell::plan("a;b;c;d;e;f;g;h;i").error);
    run("echo hello|less");assert(output=="hello\n");
    run("commands|less");assert(calls.size()==2);
    run("cat exact|less");assert(output.size()==8192);
    run("cat big|less");assert(calls.size()==1 && !context.requested_app);
    run("echo hi|less;echo wrong");assert(calls.empty());
    run("echo hi|less&&echo wrong");assert(calls.empty());
    run("echo hi|less file");assert(calls.empty());
    for(int i=0;i<100;++i)run("cat lines|grep words|less");
    run("date;time");assert(calls.size()==2);
    run("ntp&&echo wrong;echo right");assert(calls.size()==2 && output=="right\n");
    run("ntp&&echo wrong&&echo wrong;echo right");assert(calls.size()==2 && output=="right\n");
    run("echo yes&&echo next");assert(output=="yes\nnext\n");
    run("echo 'a;b|c' ; echo x\\&y");assert(output=="a;b|c\nx&y\n");
    run("echo first;echo 'bad");assert(calls.empty());
    run("echo first;edit file");assert(calls.empty());
    run("network&&echo x");assert(calls.empty());
    run("cat lines|grep words|wc -w");assert(output=="2\n" && io.command_status==0);
    run("cat lines|head -n 1|cat");assert(output=="one\n");
    run("cat lines|wc");assert(output=="2 4 18\n");
    run("cat lines|grep missing|wc -c");assert(output=="0\n");
    run("echo x\\ ;echo y");assert(output=="x \ny\n");
    run("cat lines|head -n 0|wc -c");assert(output=="0\n");
    run("cat lines|grep missing&&echo wrong;echo right");assert(output=="right\n");
    run("ntp&&cat lines|wc;echo right");assert(calls.size()==2 && output=="right\n");
    run("cat exact|wc -c");assert(output=="8192\n");
    run("cat big|wc -c&&echo wrong;echo right");assert(output.find("exceeds 8192")!=std::string::npos && calls.size()==2);
    run("cat lines|time");assert(calls.empty());
    run("date 2026-01-01|wc");assert(calls.empty());
    fail_after=1;run("echo x|wc");assert(calls.empty() && output.find("not enough PSRAM")!=std::string::npos);fail_after=-1;
    cancel_at=2;run("echo first;echo second;echo third");assert(calls.size()==1 && io.command_status==130);cancel_at=-1;
    for(int i=0;i<100;++i)run("cat lines|grep words|wc -c;echo done");
    puts("PASS: composition preflight, quoting, conditionals, bounded pipelines, filters, cancellation and allocation recovery");
}
