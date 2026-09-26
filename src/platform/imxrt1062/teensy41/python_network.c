#if SK_ETHERNET
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "network_socket.h"
extern bool sk_python_poll_cancel(void);
typedef struct { mp_obj_base_t base; int handle; int32_t timeout; } socket_obj;
static uint32_t now_ms(void) { return xTaskGetTickCount()*portTICK_PERIOD_MS; }
void sk_python_network_close_all(void) {
    sk_net_request q={.op=SK_NET_CLOSE_ALL}; sk_net_reply r;
    (void)sk_net_call(&q,&r);
}
static void check_cancel(void) {
    if (sk_python_poll_cancel()) {
        sk_python_network_close_all();
        mp_raise_type(&mp_type_KeyboardInterrupt);
    }
    mp_handle_pending(true);
}
static void check_wait(uint32_t start,int32_t timeout) {
    check_cancel();
    if (!timeout) mp_raise_OSError(EAGAIN);
    if (timeout>0 && now_ms()-start>=(uint32_t)timeout) mp_raise_OSError(ETIMEDOUT);
    vTaskDelay(pdMS_TO_TICKS(2));
}
static void call(sk_net_request *q,sk_net_reply *r) {
    int error=sk_net_call(q,r); if (error) mp_raise_OSError(error);
}
static socket_obj *socket_get(mp_obj_t obj) {
    socket_obj *s=MP_OBJ_TO_PTR(obj);
    if (!s->handle) mp_raise_OSError(EBADF);
    return s;
}
static uint16_t get_port(mp_obj_t arg) {
    mp_int_t p=mp_obj_get_int(arg);
    if (p<1 || p>65535) mp_raise_ValueError(MP_ERROR_TEXT("port must be 1..65535"));
    return p;
}
static void resolve(mp_obj_t host,uint8_t ip[4],int32_t timeout) {
    size_t len; const char *name=mp_obj_str_get_data(host,&len);
    if (!len || len>=254 || memchr(name,0,len)) mp_raise_ValueError(MP_ERROR_TEXT("invalid hostname"));
    sk_net_request q={.op=SK_NET_DNS_START}; sk_net_reply r;
    memcpy(q.host,name,len); check_cancel(); call(&q,&r);
    q.op=SK_NET_DNS_POLL; q.handle=r.value;
    uint32_t start=now_ms();
    // DNS has a bounded five-second limit even for a blocking socket.
    int32_t limit=timeout<0 || timeout>5000 ? 5000 : timeout;
    for (;;) {
        int err=sk_net_call(&q,&r);
        if (err!=EAGAIN) { if (err) mp_raise_OSError(err); memcpy(ip,r.ip,4); return; }
        check_wait(start,limit);
    }
}
static mp_obj_t socket_close(mp_obj_t obj) {
    socket_obj *s=MP_OBJ_TO_PTR(obj);
    if (s->handle) {
        sk_net_request q={.op=SK_NET_CLOSE,.handle=s->handle}; sk_net_reply r;
        (void)sk_net_call(&q,&r); s->handle=0;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_close_obj,socket_close);
static mp_obj_t socket_new(const mp_obj_type_t *type,size_t n,size_t kw,const mp_obj_t *args) {
    mp_arg_check_num(n,kw,0,3,false);
    if ((n>0 && mp_obj_get_int(args[0])!=2) || (n>1 && mp_obj_get_int(args[1])!=1) ||
        (n>2 && mp_obj_get_int(args[2])!=0 && mp_obj_get_int(args[2])!=6))
        mp_raise_OSError(EPROTONOSUPPORT);
    socket_obj *s=mp_obj_malloc_with_finaliser(socket_obj,type);
    s->handle=0; s->timeout=5000;
    sk_net_request q={.op=SK_NET_OPEN}; sk_net_reply r; call(&q,&r); s->handle=r.value;
    return MP_OBJ_FROM_PTR(s);
}
static mp_obj_t socket_timeout(mp_obj_t obj,mp_obj_t value) {
    socket_obj *s=socket_get(obj);
    if (value==mp_const_none) s->timeout=-1;
    else {
        mp_float_t seconds=mp_obj_get_float(value);
        if (!isfinite(seconds) || seconds<0 || seconds>3600) mp_raise_ValueError(MP_ERROR_TEXT("invalid timeout"));
        s->timeout=seconds==0 ? 0 : (int32_t)ceilf(seconds*1000);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_timeout_obj,socket_timeout);
static mp_obj_t socket_connect(mp_obj_t obj,mp_obj_t address) {
    socket_obj *s=socket_get(obj); size_t n; mp_obj_t *items;
    mp_obj_get_array(address,&n,&items);
    if (n!=2) mp_raise_ValueError(MP_ERROR_TEXT("expected (host, port)"));
    sk_net_request q={.op=SK_NET_CONNECT,.handle=s->handle,.port=get_port(items[1])}; sk_net_reply r;
    // A failed or interrupted connect always releases its reserved slot.
    nlr_buf_t nlr;
    if (nlr_push(&nlr)==0) {
        uint32_t start=now_ms(); resolve(items[0],q.ip,s->timeout); call(&q,&r);
        q.op=SK_NET_CONNECTED;
        for (;;) {
            int err=sk_net_call(&q,&r);
            if (err!=EAGAIN) { if (err) mp_raise_OSError(err); break; }
            check_wait(start,s->timeout);
        }
        nlr_pop();
    } else { socket_close(obj); nlr_raise(nlr.ret_val); }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_connect_obj,socket_connect);
static mp_obj_t socket_recv(mp_obj_t obj,mp_obj_t count) {
    socket_obj *s=socket_get(obj); mp_int_t size=mp_obj_get_int(count);
    if (size<0) mp_raise_ValueError(MP_ERROR_TEXT("negative receive size"));
    if (!size) return mp_obj_new_bytes(NULL,0);
    sk_net_request q={.op=SK_NET_RECV,.handle=s->handle,.length=size>SK_NET_CHUNK ? SK_NET_CHUNK : size};
    sk_net_reply r; uint32_t start=now_ms();
    for (;;) {
        check_cancel(); int err=sk_net_call(&q,&r);
        if (err!=EAGAIN) { if (err) mp_raise_OSError(err); return mp_obj_new_bytes(r.data,r.value); }
        check_wait(start,s->timeout);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recv_obj,socket_recv);
static mp_obj_t send_data(mp_obj_t obj,mp_obj_t data,bool all) {
    socket_obj *s=socket_get(obj); mp_buffer_info_t buffer;
    mp_get_buffer_raise(data,&buffer,MP_BUFFER_READ);
    sk_net_request q={.op=SK_NET_SEND,.handle=s->handle}; sk_net_reply r;
    size_t sent=0; uint32_t start=now_ms();
    while (sent<buffer.len) {
        check_cancel();
        q.length=buffer.len-sent>SK_NET_CHUNK ? SK_NET_CHUNK : buffer.len-sent;
        memcpy(q.data,(uint8_t *)buffer.buf+sent,q.length);
        int err=sk_net_call(&q,&r);
        if (err==EAGAIN) { check_wait(start,s->timeout); continue; }
        if (err) mp_raise_OSError(err);
        sent+=r.value;
        if (!all) break;
        if (sent<buffer.len) {
            if (s->timeout>0 && now_ms()-start>=(uint32_t)s->timeout) mp_raise_OSError(ETIMEDOUT);
            vTaskDelay(1);
        }
    }
    return all ? mp_const_none : mp_obj_new_int_from_uint(sent);
}
static mp_obj_t socket_send(mp_obj_t o,mp_obj_t b) { return send_data(o,b,false); }
static mp_obj_t socket_sendall(mp_obj_t o,mp_obj_t b) { return send_data(o,b,true); }
static MP_DEFINE_CONST_FUN_OBJ_2(socket_send_obj,socket_send);
static MP_DEFINE_CONST_FUN_OBJ_2(socket_sendall_obj,socket_sendall);
static mp_obj_t socket_enter(mp_obj_t obj) { socket_get(obj); return obj; }
static MP_DEFINE_CONST_FUN_OBJ_1(socket_enter_obj,socket_enter);
static mp_obj_t socket_exit(size_t n,const mp_obj_t *args) { (void)n; socket_close(args[0]); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_exit_obj,4,4,socket_exit);
static mp_obj_t getaddrinfo(size_t n,const mp_obj_t *args) {
    if ((n>2 && mp_obj_get_int(args[2])!=0 && mp_obj_get_int(args[2])!=2) ||
        (n>3 && mp_obj_get_int(args[3])!=0 && mp_obj_get_int(args[3])!=1) ||
        (n>4 && mp_obj_get_int(args[4])!=0 && mp_obj_get_int(args[4])!=6) ||
        (n>5 && mp_obj_get_int(args[5])!=0)) mp_raise_OSError(EPROTONOSUPPORT);
    uint16_t port=get_port(args[1]); uint8_t ip[4]; resolve(args[0],ip,5000);
    char text[16]; snprintf(text,sizeof(text),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
    mp_obj_t addr[]={mp_obj_new_str(text,strlen(text)),mp_obj_new_int(port)};
    mp_obj_t entry[]={MP_OBJ_NEW_SMALL_INT(2),MP_OBJ_NEW_SMALL_INT(1),MP_OBJ_NEW_SMALL_INT(6),mp_obj_new_str("",0),mp_obj_new_tuple(2,addr)};
    mp_obj_t tuple=mp_obj_new_tuple(5,entry); return mp_obj_new_list(1,&tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(getaddrinfo_obj,2,6,getaddrinfo);
static void put(mp_obj_dict_t *dict,const char *name,mp_obj_t value) {
    mp_obj_dict_store(MP_OBJ_FROM_PTR(dict),MP_OBJ_NEW_QSTR(qstr_from_str(name)),value);
}
void sk_python_network_init(void) {
    mp_obj_module_t *module=MP_OBJ_TO_PTR(mp_obj_new_module(qstr_from_str("socket")));
    mp_obj_full_type_t *type=m_new_obj(mp_obj_full_type_t); memset(type,0,sizeof(*type));
    type->base.type=&mp_type_type; type->name=qstr_from_str("socket");
    MP_OBJ_TYPE_SET_SLOT(type,make_new,socket_new,0);
    mp_obj_dict_t *methods=MP_OBJ_TO_PTR(mp_obj_new_dict(0));
    MP_OBJ_TYPE_SET_SLOT(type,locals_dict,methods,1);
    // The module roots the dynamically allocated type and method table.
    put(module->globals,"socket",MP_OBJ_FROM_PTR(type));
    put(methods,"close",MP_OBJ_FROM_PTR(&socket_close_obj));
    put(methods,"__del__",MP_OBJ_FROM_PTR(&socket_close_obj));
    put(methods,"__enter__",MP_OBJ_FROM_PTR(&socket_enter_obj));
    put(methods,"__exit__",MP_OBJ_FROM_PTR(&socket_exit_obj));
    put(methods,"connect",MP_OBJ_FROM_PTR(&socket_connect_obj));
    put(methods,"recv",MP_OBJ_FROM_PTR(&socket_recv_obj));
    put(methods,"send",MP_OBJ_FROM_PTR(&socket_send_obj));
    put(methods,"sendall",MP_OBJ_FROM_PTR(&socket_sendall_obj));
    put(methods,"settimeout",MP_OBJ_FROM_PTR(&socket_timeout_obj));
    put(module->globals,"getaddrinfo",MP_OBJ_FROM_PTR(&getaddrinfo_obj));
    put(module->globals,"AF_INET",MP_OBJ_NEW_SMALL_INT(2));
    put(module->globals,"SOCK_STREAM",MP_OBJ_NEW_SMALL_INT(1));
    put(module->globals,"IPPROTO_TCP",MP_OBJ_NEW_SMALL_INT(6));
}
#endif
