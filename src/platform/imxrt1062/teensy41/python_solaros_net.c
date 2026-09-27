#if SK_ETHERNET
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "solar_os_net_session.h"
#include "solar_os_network.h"
extern bool sk_python_poll_cancel(void);
static solar_os_net_session_t *python_net_session;
static bool cancelled;
static bool python_should_cancel(void *user) { (void)user; cancelled |= sk_python_poll_cancel(); return cancelled; }
static void python_raise_esp(esp_err_t err) {
    if (cancelled) { cancelled=false; mp_raise_type(&mp_type_KeyboardInterrupt); }
    mp_raise_msg_varg(&mp_type_OSError,MP_ERROR_TEXT("%s"),esp_err_to_name(err));
}
static void python_check_esp(esp_err_t err) { if (err!=ESP_OK) python_raise_esp(err); }
static uint32_t python_u32_from_obj(mp_obj_t obj) {
    mp_int_t value=mp_obj_get_int(obj);
    if (value<0) mp_raise_ValueError(MP_ERROR_TEXT("expected unsigned integer"));
    return value;
}
static uint32_t python_optional_u32(size_t n,const mp_obj_t *args,size_t i,uint32_t fallback) {
    return i<n && args[i]!=mp_const_none ? python_u32_from_obj(args[i]) : fallback;
}
static mp_obj_t python_key(const char *s) { return MP_OBJ_NEW_QSTR(qstr_from_str(s)); }
static void python_dict_store_uint(mp_obj_t d,const char *k,uint32_t v) { mp_obj_dict_store(d,python_key(k),mp_obj_new_int_from_uint(v)); }
static void python_dict_store_bool(mp_obj_t d,const char *k,bool v) { mp_obj_dict_store(d,python_key(k),mp_obj_new_bool(v)); }
static void python_dict_store_cstr(mp_obj_t d,const char *k,const char *v) { mp_obj_dict_store(d,python_key(k),mp_obj_new_str(v,strlen(v))); }
static solar_os_net_session_t *python_net_get(void) {
    if (!python_net_session) python_check_esp(solar_os_net_session_create("python.app",python_should_cancel,NULL,&python_net_session));
    return python_net_session;
}
static uint16_t python_net_port(mp_obj_t obj) {
    uint32_t v=python_u32_from_obj(obj);
    if (!v || v>65535) mp_raise_ValueError(MP_ERROR_TEXT("expected port 1..65535"));
    return v;
}
static size_t python_net_receive_size(size_t n,const mp_obj_t *args,size_t i) {
    uint32_t v=i<n ? python_u32_from_obj(args[i]) : 4096;
    if (!v || v>SOLAR_OS_NET_MAX_TRANSFER_BYTES) mp_raise_ValueError(MP_ERROR_TEXT("receive size out of range"));
    return v;
}
static uint32_t python_net_timeout(size_t n,const mp_obj_t *args,size_t i,uint32_t fallback) {
    uint32_t v=python_optional_u32(n,args,i,fallback);
    if (v>SOLAR_OS_NET_MAX_TIMEOUT_MS) mp_raise_ValueError(MP_ERROR_TEXT("timeout out of range"));
    return v;
}
#include "solar_os_python_net.inc"
void sk_python_solaros_net_destroy(void) {
    solar_os_net_session_destroy(python_net_session); python_net_session=NULL; cancelled=false;
}
void sk_python_solaros_net_init(void) {
    cancelled=false;
    mp_obj_t root=mp_obj_new_module(qstr_from_str("solaros"));
    mp_obj_t net=mp_obj_new_module(qstr_from_str("solaros.net"));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(((mp_obj_module_t *)MP_OBJ_TO_PTR(root))->globals),python_key("net"),net);
    mp_obj_t globals=MP_OBJ_FROM_PTR(((mp_obj_module_t *)MP_OBJ_TO_PTR(net))->globals);
#define METHOD(name) mp_obj_dict_store(globals,python_key(#name),MP_OBJ_FROM_PTR(&solaros_net_##name##_obj))
    METHOD(tcp_connect); METHOD(tcp_send); METHOD(tcp_receive);
    METHOD(udp_open); METHOD(udp_send); METHOD(udp_receive);
    METHOD(websocket_connect); METHOD(websocket_send); METHOD(websocket_receive);
    METHOD(close); METHOD(close_all); METHOD(limits); METHOD(router_start); METHOD(router_stop); METHOD(ping);
#undef METHOD
}
#endif
