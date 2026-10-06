#if SK_FTP
#include <arduino_freertos.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include "network_socket.h"
#include "ftp_socket.h"
namespace {
struct Socket {
    bool used=false, stopped=false, listener=false;
    int handle=0;
    uint16_t port=0;
    uint32_t receive_ms=10000, send_ms=10000;
    sockaddr_in peer{};
    bool (*cancel)(void *)=nullptr;
    void *cancel_user=nullptr;
};
Socket sockets[8];
TaskHandle_t daemon_task;
volatile bool daemon_finished;
int fail(int error) {errno=error;return -1;}
Socket *lookup(int fd) {
    if(fd<0 || fd>=8 || !sockets[fd].used){errno=EBADF;return nullptr;}
    return &sockets[fd];
}
int allocate() {
    taskENTER_CRITICAL();
    for(unsigned i=0;i<8;++i)if(!sockets[i].used) {
        sockets[i]=Socket{};sockets[i].used=true;taskEXIT_CRITICAL();return i;
    }
    taskEXIT_CRITICAL();return fail(EMFILE);
}
bool stopped(Socket *s) {
    taskENTER_CRITICAL();bool value=s->stopped;taskEXIT_CRITICAL();return value || (s->cancel && s->cancel(s->cancel_user));
}
int call(Socket *s,int op,sk_net_reply &r) {
    sk_net_request q{};q.op=op;q.handle=s->handle;return sk_net_call(&q,&r);
}
int transfer(int fd,void *data,size_t size,bool writing,int flags) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(flags || s->listener || !s->handle)return fail(EINVAL);
    if(!size)return 0;
    uint32_t start=millis(),timeout=writing?s->send_ms:s->receive_ms;
    do {
        if(stopped(s))return fail(ECANCELED);
        sk_net_request q{};sk_net_reply r{};
        q.op=writing?SK_NET_SEND:SK_NET_RECV;q.handle=s->handle;
        q.length=std::min(size,size_t(SK_NET_CHUNK));
        if(writing)memcpy(q.data,data,q.length);
        int error=sk_net_call(&q,&r);
        if(!error) {if(!writing && r.value>0)memcpy(data,r.data,r.value);return r.value;}
        if(error!=EAGAIN)return fail(error);
        if(millis()-start>=timeout)return fail(ETIMEDOUT);
        vTaskDelay(1);
    }while(true);
}
}
extern "C" void sk_ftp_set_cancel(int fd,bool (*fn)(void *),void *user) {
    Socket *s=lookup(fd);if(s){s->cancel=fn;s->cancel_user=user;}
}
extern "C" int sk_ftp_socket(int family,int type,int protocol) {
    if(family!=AF_INET || type!=SOCK_STREAM || protocol!=IPPROTO_TCP)return fail(EINVAL);
    return allocate();
}
extern "C" int sk_ftp_close(int fd) {
    Socket *s=lookup(fd);if(!s)return -1;
    sk_net_reply r{};
    if(s->handle)call(s,s->listener?SK_NET_FTP_END:SK_NET_CLOSE,r);
    taskENTER_CRITICAL();*s=Socket{};taskEXIT_CRITICAL();return 0;
}
extern "C" int sk_ftp_shutdown(int fd,int) {
    Socket *s=lookup(fd);if(!s)return -1;
    taskENTER_CRITICAL();s->stopped=true;taskEXIT_CRITICAL();return 0;
}
extern "C" int sk_ftp_setsockopt(int fd,int level,int option,const void *value,socklen_t length) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(level!=SOL_SOCKET || !value)return fail(EINVAL);
    if(option==SO_REUSEADDR)return 0;
    if((option!=SO_RCVTIMEO && option!=SO_SNDTIMEO) || length!=sizeof(timeval))return fail(EINVAL);
    auto *t=static_cast<const timeval *>(value);
    if(t->tv_sec<0 || t->tv_usec<0 || t->tv_usec>=1000000)return fail(EINVAL);
    uint64_t ms=uint64_t(t->tv_sec)*1000+(t->tv_usec+999)/1000;
    if(ms>UINT32_MAX)return fail(EINVAL);
    (option==SO_RCVTIMEO?s->receive_ms:s->send_ms)=ms?ms:10000;return 0;
}
extern "C" int sk_ftp_bind(int fd,const sockaddr *address,socklen_t size) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(!address || size!=sizeof(sockaddr_in) || address->sa_family!=AF_INET || s->handle)return fail(EINVAL);
    s->port=ntohs(reinterpret_cast<const sockaddr_in *>(address)->sin_port);return 0;
}
extern "C" int sk_ftp_listen(int fd,int) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(s->handle)return fail(EINVAL);
    sk_net_request q{};sk_net_reply r{};q.op=SK_NET_FTP_LISTEN;q.port=s->port;
    int error=sk_net_call(&q,&r);if(error)return fail(error);
    s->handle=r.value;s->port=r.port;s->listener=true;return 0;
}
extern "C" int sk_ftp_connect(int fd,const sockaddr *address,socklen_t size) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(!address || size!=sizeof(sockaddr_in) || address->sa_family!=AF_INET || s->handle)return fail(EINVAL);
    sk_net_reply r{};int error=call(s,SK_NET_SERVICE_OPEN,r);if(error)return fail(error);
    s->handle=r.value;s->peer=*reinterpret_cast<const sockaddr_in *>(address);
    sk_net_request q{};q.op=SK_NET_CONNECT;q.handle=s->handle;
    memcpy(q.ip,&s->peer.sin_addr.s_addr,4);q.port=ntohs(s->peer.sin_port);
    error=sk_net_call(&q,&r);if(error)return fail(error);
    uint32_t start=millis();
    do {
        if(stopped(s))return fail(ECANCELED);
        error=call(s,SK_NET_CONNECTED,r);if(!error)return 0;
        if(error!=EAGAIN)return fail(error);
        if(millis()-start>=s->send_ms)return fail(ETIMEDOUT);
        vTaskDelay(1);
    }while(true);
}
extern "C" int sk_ftp_accept(int fd,sockaddr *address,socklen_t *length) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(!s->listener || stopped(s))return fail(EINVAL);
    int child=allocate();if(child<0)return -1;
    sk_net_reply r{};int error=call(s,SK_NET_FTP_ACCEPT,r);
    if(error){sk_ftp_close(child);return fail(error);}
    auto &c=sockets[child];c.handle=r.value;c.port=s->port;
    c.peer.sin_family=AF_INET;c.peer.sin_port=htons(r.port);memcpy(&c.peer.sin_addr.s_addr,r.ip,4);
    if(address && length) {memcpy(address,&c.peer,std::min(size_t(*length),sizeof(c.peer)));*length=sizeof(c.peer);}
    return child;
}
extern "C" int sk_ftp_getsockname(int fd,sockaddr *address,socklen_t *length) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(!address || !length || *length<sizeof(sockaddr_in))return fail(EINVAL);
    sk_net_reply r{};int error=call(s,SK_NET_LOCAL_ADDR,r);if(error)return fail(error);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(s->port);
    memcpy(&local.sin_addr.s_addr,r.ip,4);memcpy(address,&local,sizeof(local));*length=sizeof(local);return 0;
}
extern "C" int sk_ftp_getpeername(int fd,sockaddr *address,socklen_t *length) {
    Socket *s=lookup(fd);if(!s)return -1;
    if(!address || !length || *length<sizeof(sockaddr_in))return fail(EINVAL);
    memcpy(address,&s->peer,sizeof(s->peer));*length=sizeof(s->peer);return 0;
}
extern "C" ssize_t sk_ftp_send(int fd,const void *data,size_t size,int flags) {return transfer(fd,const_cast<void *>(data),size,true,flags);}
extern "C" ssize_t sk_ftp_recv(int fd,void *data,size_t size,int flags) {return transfer(fd,data,size,false,flags);}
extern "C" int sk_ftp_select(int count,fd_set *reads,fd_set *writes,fd_set *excepts,timeval *timeout) {
    if(count<0 || count>8 || !reads || writes || excepts || !timeout)return fail(EINVAL);
    fd_set requested=*reads;uint32_t start=millis();
    uint64_t limit=uint64_t(timeout->tv_sec)*1000+(timeout->tv_usec+999)/1000;
    do {
        FD_ZERO(reads);int ready=0;
        for(int fd=0;fd<count;++fd)if(FD_ISSET(fd,&requested)) {
            Socket *s=lookup(fd);if(!s)return -1;
            if(stopped(s))return fail(ECANCELED);
            sk_net_reply r{};int error=call(s,s->listener?SK_NET_FTP_POLL:SK_NET_POLL,r);
            if(error)return fail(error);
            if(r.value){FD_SET(fd,reads);++ready;}
        }
        if(ready || millis()-start>=limit)return ready;
        vTaskDelay(1);
    }while(true);
}
extern "C" int sk_ftp_inet_pton(int family,const char *text,void *out) {
    return family==AF_INET && text && out?ip4addr_aton(text,static_cast<ip4_addr_t *>(out)):0;
}
extern "C" void esp_fill_random(void *,size_t);
extern "C" uint32_t sk_ftp_random() {uint32_t value;esp_fill_random(&value,sizeof(value));return value;}
extern "C" void sk_ftp_task_reap() {
    if(daemon_task && daemon_finished && eTaskGetState(daemon_task)==eSuspended) {
        solar_os_task_delete_external(daemon_task);daemon_task=nullptr;
    }
}
extern "C" void sk_ftp_task_delete(TaskHandle_t task) {
    // eTaskGetState is only a snapshot: a live task blocked indefinitely on
    // a mutex can appear suspended while another task wakes it. Require an
    // explicit terminal marker before the reaper is allowed to delete it.
    daemon_finished=true;
    solar_os_task_delete_internal(task);
}
extern "C" BaseType_t sk_ftp_task_create(TaskFunction_t fn,const char *name,uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t core,solar_os_task_role_t role) {
    sk_ftp_task_reap();if(daemon_task)return pdFAIL;
    daemon_finished=false;
    BaseType_t result=solar_os_task_create_pinned_external(fn,name,bytes,arg,priority,&daemon_task,core,role);
    *out=daemon_task;return result;
}
#endif
