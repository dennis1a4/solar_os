#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <set>
#include <vector>
#include "network_socket.h"
#include "ftp_socket.h"
uint32_t fake_millis;
static int next_handle=100, rpc_error=0;
static bool stall=false, eof=false;
static std::set<int> connections,listeners;
static std::vector<unsigned char> payload;
extern "C" int sk_net_call(sk_net_request *q,sk_net_reply *r) {
    *r={};
    if(q->op==SK_NET_CLOSE){connections.erase(q->handle);return 0;}
    if(q->op==SK_NET_FTP_END){listeners.erase(q->handle);return 0;}
    if(rpc_error)return rpc_error;
    switch(q->op) {
    case SK_NET_SERVICE_OPEN:r->value=++next_handle;connections.insert(r->value);return 0;
    case SK_NET_FTP_LISTEN:r->value=++next_handle;r->port=q->port?q->port:49152;listeners.insert(r->value);return 0;
    case SK_NET_FTP_ACCEPT:r->value=++next_handle;connections.insert(r->value);r->ip[0]=127;r->ip[3]=1;r->port=1234;return 0;
    case SK_NET_CONNECT:return 0;
    case SK_NET_CONNECTED:return stall?EAGAIN:0;
    case SK_NET_LOCAL_ADDR:r->ip[0]=127;r->ip[3]=1;return 0;
    case SK_NET_FTP_POLL:case SK_NET_POLL:r->value=!stall;return 0;
    case SK_NET_SEND:
        if(stall)return EAGAIN;
        r->value=q->length;payload.insert(payload.end(),q->data,q->data+q->length);return 0;
    case SK_NET_RECV:
        if(stall)return EAGAIN;
        if(eof)return 0;
        r->value=std::min(size_t(q->length),payload.size());
        memcpy(r->data,payload.data(),r->value);payload.erase(payload.begin(),payload.begin()+r->value);return 0;
    default:assert(false);return EINVAL;
    }
}
extern "C" int ip4addr_aton(const char *,ip4_addr_t *out) {out->addr=htonl(0x7f000001);return 1;}
extern "C" void esp_fill_random(void *out,size_t len) {memset(out,0x42,len);}
static int task_state=eSuspended;
static unsigned deleted_tasks;
struct ParkedTask {};
extern "C" int eTaskGetState(TaskHandle_t) {return task_state;}
extern "C" BaseType_t solar_os_task_create_pinned_external(TaskFunction_t,const char *,uint32_t,void *,UBaseType_t,TaskHandle_t *out,BaseType_t,solar_os_task_role_t) {*out=(void *)1;return pdPASS;}
extern "C" void solar_os_task_delete_external(TaskHandle_t) {++deleted_tasks;}
extern "C" void solar_os_task_delete_internal(TaskHandle_t) {throw ParkedTask{};}
static bool cancelled(void *) {return fake_millis>=5;}
static int connected() {
    int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(fd>=0);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(21);
    assert(connect(fd,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))==0);return fd;
}
int main() {
    for(unsigned cycle=0;cycle<100;++cycle) {
        TaskHandle_t task=nullptr;
        assert(sk_ftp_task_create(nullptr,"ftpd",16384,nullptr,1,&task,0,0)==pdPASS);
        // Simulate a live worker whose scheduler snapshot says suspended.
        sk_ftp_task_reap();assert(deleted_tasks==cycle);
        assert(sk_ftp_task_create(nullptr,"ftpd",16384,nullptr,1,&task,0,0)==pdFAIL);
        try {sk_ftp_task_delete(nullptr);} catch(const ParkedTask &) {}
        task_state=0;sk_ftp_task_reap();assert(deleted_tasks==cycle);
        task_state=eSuspended;sk_ftp_task_reap();assert(deleted_tasks==cycle+1);
    }
    for(int cycle=0;cycle<100;++cycle) {
        int fd=connected();unsigned char input[1700],output[1700];
        for(unsigned i=0;i<sizeof(input);++i)input[i]=i;
        size_t sent=0,received=0;
        while(sent<sizeof(input)){auto n=send(fd,input+sent,sizeof(input)-sent,0);assert(n>0 && n<=SK_NET_CHUNK);sent+=n;}
        while(received<sizeof(output)){auto n=recv(fd,output+received,sizeof(output)-received,0);assert(n>0);received+=n;}
        assert(!memcmp(input,output,sizeof(input)));eof=true;assert(recv(fd,output,1,0)==0);eof=false;
        assert(close(fd)==0 && connections.empty());
    }
    int fd=connected();timeval timeout{0,3000};assert(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    stall=true;char byte;fake_millis=0;assert(recv(fd,&byte,1,0)==-1 && errno==ETIMEDOUT && fake_millis==3);
    fake_millis=0;sk_ftp_set_cancel(fd,cancelled,nullptr);timeout={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    assert(recv(fd,&byte,1,0)==-1 && errno==ECANCELED && fake_millis==5);
    close(fd);stall=false;
    fd=connected();shutdown(fd,SHUT_RDWR);assert(recv(fd,&byte,1,0)==-1 && errno==ECANCELED);close(fd);
    fd=connected();rpc_error=ENETDOWN;assert(recv(fd,&byte,1,0)==-1 && errno==ENETDOWN);close(fd);rpc_error=0;
    int server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in addr{};addr.sin_family=AF_INET;
    assert(bind(server,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))==0 && listen(server,1)==0);
    socklen_t size=sizeof(addr);assert(getsockname(server,reinterpret_cast<sockaddr *>(&addr),&size)==0 && ntohs(addr.sin_port)==49152);
    fd_set reads;FD_ZERO(&reads);FD_SET(server,&reads);timeout={0,2000};stall=true;fake_millis=0;
    assert(select(server+1,&reads,nullptr,nullptr,&timeout)==0 && fake_millis==2);stall=false;
    FD_SET(server,&reads);assert(select(server+1,&reads,nullptr,nullptr,&timeout)==1);
    fd=accept(server,nullptr,nullptr);assert(fd>=0);close(server);assert(listeners.empty() && connections.size()==1);
    assert(getpeername(fd,reinterpret_cast<sockaddr *>(&addr),&size)==0 && ntohs(addr.sin_port)==1234);close(fd);
    int slots[8];for(auto &slot:slots){slot=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(slot>=0);}
    assert(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)==-1 && errno==EMFILE);for(auto slot:slots)close(slot);
    assert(connections.empty() && listeners.empty());puts("FTP socket adapter: PASS");
}
