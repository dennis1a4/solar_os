#if SK_SHELL_COMPOSE
#include "shell_plan.h"
#include <stdio.h>
#include <stdlib.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_shell_parse.h"
#include "solar_os_memory.h"
bool sk_console_poll_cancel(bool);
}
namespace {
constexpr size_t pipe_capacity=8192, pipe_budget=65536;
// Access serialized by console_gate, including allocation and free. Other
// consoles may execute at explicit polling points, so account globally.
size_t pipe_bytes;
struct Buffer {char *data=nullptr;size_t size=0;bool overflow=false;};
esp_err_t capture(const char *data,size_t size,void *user) {
    auto &b=*static_cast<Buffer *>(user);
    if(size>pipe_capacity-b.size){b.overflow=true;return ESP_ERR_NO_MEM;}
    memcpy(b.data+b.size,data,size);b.size+=size;return ESP_OK;
}
bool member(const char *name,const char *const *names,size_t n) {
    for(size_t i=0;i<n;++i)if(!strcmp(name,names[i]))return true;
    return false;
}
template<size_t N> bool member(const char *name,const char *const (&names)[N]) {return member(name,names,N);}
bool filter(const char *name) {return !strcmp(name,"grep") || !strcmp(name,"head") || !strcmp(name,"wc");}
bool status_known(const char *name) {
    static const char *const names[]={"echo","date","time","rtc","ntp","setterm","identity","mem","uptime","status","top","df","port","cd","cat","mkdir","cp","mv","rm","ls","pwd","version","board","grep","head","wc"};
    return member(name,names);
}
bool synchronous(const char *name) {
    static const char *const names[]={"mem","uptime","status","top","df","port","commands","apps","setterm","identity","network","ramfs","flash","sd","usb","gpio","i2c","spi","uart","expansion","ping","netscan","jobs"};
    return status_known(name) || member(name,names);
}
bool pipe_source(int argc,char **argv) {
    static const char *const names[]={"echo","cat","ls","grep","head","wc"};
    static const char *const snapshots[]={"date","time","rtc","pwd","version","board","mem","uptime","status","top","df","port"};
    return member(argv[0],names) || (argc==1 && member(argv[0],snapshots));
}
bool slice(const char *line,const skshell::Step &step,char text[192],char **argv,int &argc) {
    size_t n=step.end-step.begin;memcpy(text,line+step.begin,n);text[n]=0;
    const auto r=solar_os_shell_tokenize(text,argv,20);argc=r.argc;
    return r.error==SOLAR_OS_SHELL_PARSE_OK && argc;
}
void error(solar_os_shell_io_t *io,const char *text) {
    io->command_status=2;solar_os_shell_io_printf(io,"shell: %s\n",text);
}
}

extern "C" void sk_shell_cmd_filter(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);io->command_status=2;
    if(!io->command_input){error(io,"grep/head/wc require pipe input");return;}
    size_t limit=10;
    if(!strcmp(argv[0],"grep")) {
        if(argc!=2){error(io,"usage: ... | grep LITERAL (case sensitive)");return;}
    } else if(!strcmp(argv[0],"head")) {
        if(argc!=1 && !(argc==3 && !strcmp(argv[1],"-n"))){error(io,"usage: ... | head [-n COUNT]");return;}
        if(argc==3) {
            limit=0;
            if(!*argv[2]){error(io,"invalid head count");return;}
            for(const char *p=argv[2];*p;++p) {
                if(*p<'0' || *p>'9' || limit>8192){error(io,"head count must be 0..8192");return;}
                limit=limit*10+size_t(*p-'0');
            }
            if(limit>8192){error(io,"head count must be 0..8192");return;}
        }
    } else if(argc!=1 && !(argc==2 && (!strcmp(argv[1],"-l") || !strcmp(argv[1],"-c") || !strcmp(argv[1],"-w")))) {
        error(io,"usage: ... | wc [-l|-w|-c]");return;
    }
    io->command_status=0;
    const char *data=io->command_input;const size_t size=io->command_input_size;
    size_t lines=0,words=0;bool in_word=false;
    if(!strcmp(argv[0],"wc")) {
        for(size_t i=0;i<size;++i) {
            lines+=data[i]=='\n';bool word=!isspace((unsigned char)data[i]);
            words+=word && !in_word;in_word=word;
        }
        if(argc==1)solar_os_shell_io_printf(io,"%lu %lu %lu\n",(unsigned long)lines,(unsigned long)words,(unsigned long)size);
        else solar_os_shell_io_printf(io,"%lu\n",(unsigned long)(argv[1][1]=='l'?lines:argv[1][1]=='w'?words:size));
        return;
    }
    bool found=false;
    for(size_t begin=0;begin<size;) {
        size_t end=begin;while(end<size && data[end]!='\n')++end;
        const size_t length=end-begin;bool emit=false;
        if(!strcmp(argv[0],"head"))emit=lines++<limit;
        else {
            const size_t needle=strlen(argv[1]);
            for(size_t j=0;needle<=length && j<=length-needle;++j)
                if(!memcmp(data+begin+j,argv[1],needle)){emit=true;break;}
        }
        if(end<size)++end;
        if(emit){found=true;if(solar_os_shell_io_write_len(io,data+begin,end-begin)!=ESP_OK)return;}
        begin=end;
    }
    if(!strcmp(argv[0],"grep") && !found)io->command_status=1;
}

// -1 leaves a single command to the existing dispatcher. All composition runs
// synchronously and never creates a task. Interactive apps are rejected during
// preflight; their asynchronous lifecycle cannot be treated as command exit.
extern "C" int sk_shell_compose(solar_os_context_t *ctx,const char *line) {
    const auto plan=skshell::plan(line);
    if(!plan.composed && !plan.error)return -1;
    auto *io=solar_os_context_shell_io(ctx);
    if(plan.error){error(io,plan.error);return 1;}
    char text[192];char *argv[20];int argc=0;bool needs_pipe=false;
    for(unsigned i=0;i<plan.count;++i) {
        if(!slice(line,plan.steps[i],text,argv,argc)){error(io,"invalid command syntax");return 1;}
        const bool input=i && plan.steps[i-1].next==skshell::Pipe;
        const bool output=plan.steps[i].next==skshell::Pipe;
        if(!synchronous(argv[0])){error(io,"command is not supported in a chain; run it separately");return 1;}
        if(plan.steps[i].next==skshell::Success && !status_known(argv[0])) {
            error(io,"command has no exit status for &&; use ; or run it separately");return 1;
        }
        if(input && strcmp(argv[0],"cat") && !filter(argv[0])){error(io,"pipe consumers: cat, grep, head, wc");return 1;}
        if(input && !strcmp(argv[0],"cat") && argc!=1){error(io,"pipe input requires cat without a filename");return 1;}
        if(output && !pipe_source(argc,argv)){error(io,"command cannot produce pipe input");return 1;}
        if(filter(argv[0]) && !input){error(io,"filter requires pipe input");return 1;}
        needs_pipe|=output;
    }
    Buffer buffers[2];
    if(needs_pipe) {
        if(pipe_bytes+2*pipe_capacity>pipe_budget){error(io,"pipe memory budget exhausted");return 1;}
        for(auto &b:buffers)b.data=static_cast<char *>(solar_os_memory_alloc(pipe_capacity,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM,"shell.pipe"));
        if(!buffers[0].data || !buffers[1].data) {
            for(auto &b:buffers)solar_os_memory_free(b.data);
            error(io,"not enough PSRAM for pipe buffers");return 1;
        }
        pipe_bytes+=2*pipe_capacity;
    }
    io->command_cancelled=false;int status=0;bool run=true;unsigned current=0;
    for(unsigned i=0;i<plan.count;) {
        // A pipeline is one conditional unit. Failure skips its remaining
        // consumers; ; starts a new unit, && checks the previous unit status.
        unsigned last=i;while(last+1<plan.count && plan.steps[last].next==skshell::Pipe)++last;
        if(run) {
            for(unsigned j=i;j<=last;++j) {
                if(sk_console_poll_cancel(true)){io->command_cancelled=true;status=130;break;}
                const auto &step=plan.steps[j];const size_t n=step.end-step.begin;
                slice(line,step,text,argv,argc);
                const bool grep_stage=!strcmp(argv[0],"grep");
                memcpy(text,line+step.begin,n);text[n]=0;
                Buffer &out=buffers[current];Buffer &in=buffers[1-current];
                out.size=0;out.overflow=false;
                io->command_input=j>i?in.data:nullptr;io->command_input_size=j>i?in.size:0;
                io->command_output_fn=j<last?capture:nullptr;io->command_output_user=&out;
                io->command_status=-1;
                const auto result=solar_os_shell_execute_command(ctx,text);
                status=result==ESP_OK?io->command_status:1;
                io->command_output_fn=nullptr;io->command_output_user=nullptr;
                io->command_input=nullptr;io->command_input_size=0;
                if(io->command_cancelled){status=130;break;}
                if(out.overflow){error(io,"pipe output exceeds 8192 bytes; consumers were not run");status=1;break;}
                if(status>0 && !(grep_stage && status==1)) {
                    // Failed producer diagnostics remain visible, never fed
                    // into a consumer as if they were successful data.
                    if(j<last)solar_os_shell_io_write_len(io,out.data,out.size);
                    break;
                }
                current=1-current;
            }
        }
        if(io->command_cancelled)break;
        run=plan.steps[last].next!=skshell::Success || status==0;
        i=last+1;
    }
    if(needs_pipe){for(auto &b:buffers)solar_os_memory_free(b.data);pipe_bytes-=2*pipe_capacity;}
    io->command_status=status;
    return 1;
}
#endif
