#if SK_HW_RESOURCES
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "python_hardware_io.h"
#include "solar_os.h"
#include "solar_os_shell.h"
extern bool sk_python_poll_cancel(void);
static solar_os_context_t *hw_context;
static void check(esp_err_t e) {
    if(e==ESP_OK)return;
    mp_raise_OSError(e==ESP_ERR_NO_MEM?ENOMEM:e==ESP_ERR_INVALID_STATE?EBUSY:e==ESP_ERR_NOT_FOUND?ENODEV:e==ESP_ERR_TIMEOUT?ETIMEDOUT:e==ESP_ERR_NOT_SUPPORTED?ENOSYS:e==ESP_ERR_INVALID_ARG||e==ESP_ERR_INVALID_SIZE?EINVAL:EIO);
}
static mp_obj_t hw_open(size_t n,const mp_obj_t *a) {
    (void)n;uint32_t token=0;check(sk_py_hw_open(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),mp_obj_get_int(a[2]),mp_obj_get_int(a[3]),&token));return mp_obj_new_int_from_uint(token);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hw_open_obj,4,4,hw_open);
static mp_obj_t hw_close(mp_obj_t h) {check(sk_py_hw_close(mp_obj_get_int(h)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(hw_close_obj,hw_close);
static mp_obj_t hw_value(mp_obj_t h,mp_obj_t op,mp_obj_t v) {int r;check(sk_py_hw_value(mp_obj_get_int(h),mp_obj_get_int(op),mp_obj_get_int(v),&r));return mp_obj_new_int(r);}
static MP_DEFINE_CONST_FUN_OBJ_3(hw_value_obj,hw_value);
static mp_obj_t hw_transfer(size_t n,const mp_obj_t *a) {
    (void)n;if(sk_python_poll_cancel())mp_raise_type(&mp_type_KeyboardInterrupt);
    mp_buffer_info_t tx={0},rx={0};
    if(a[2]!=mp_const_none)mp_get_buffer_raise(a[2],&tx,MP_BUFFER_READ);
    if(a[3]!=mp_const_none)mp_get_buffer_raise(a[3],&rx,MP_BUFFER_WRITE);
    size_t received=0;check(sk_py_hw_transfer(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),tx.buf,tx.len,rx.buf,rx.len,&received));return mp_obj_new_int_from_uint(received);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hw_transfer_obj,4,4,hw_transfer);
static mp_obj_t ticks(mp_obj_t us) {return mp_obj_new_int_from_uint(sk_py_ticks(mp_obj_is_true(us)));}
static MP_DEFINE_CONST_FUN_OBJ_1(ticks_obj,ticks);
static mp_obj_t delay(mp_obj_t duration) {
    mp_int_t ms=mp_obj_get_int(duration);if(ms<0)mp_raise_ValueError(MP_ERROR_TEXT("negative sleep"));
    while(ms) {if(sk_python_poll_cancel())mp_raise_type(&mp_type_KeyboardInterrupt);unsigned chunk=ms>10?10:ms;sk_py_delay(chunk);ms-=chunk;}
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(delay_obj,delay);
static mp_obj_t epoch(void) {return mp_obj_new_int_from_ll(time(NULL));}
static MP_DEFINE_CONST_FUN_OBJ_0(epoch_obj,epoch);
static void path(mp_obj_t obj,char *out) {if(solar_os_micropython_resolve_path(mp_obj_str_get_str(obj),out,SOLAR_OS_MICROPYTHON_PATH_MAX))mp_raise_OSError(errno);}
static mp_obj_t fs(size_t n,const mp_obj_t *a) {
    int op=mp_obj_get_int(a[0]);char p[SOLAR_OS_MICROPYTHON_PATH_MAX],q[SOLAR_OS_MICROPYTHON_PATH_MAX];
    if(op==0){if(solar_os_micropython_resolve_path(".",p,sizeof(p)))mp_raise_OSError(errno);return mp_obj_new_str(p,strlen(p));}
    if(n<2)mp_raise_TypeError(MP_ERROR_TEXT("path required"));path(a[1],p);
    int rc=0;
    if(op==1) {
        DIR *d=opendir(p);if(!d)mp_raise_OSError(errno);
        nlr_buf_t guard;if(nlr_push(&guard)){closedir(d);nlr_jump(guard.ret_val);}
        mp_obj_t list=mp_obj_new_list(0,NULL);struct dirent *entry;
        while((entry=readdir(d)))if(strcmp(entry->d_name,".") && strcmp(entry->d_name,".."))mp_obj_list_append(list,mp_obj_new_str(entry->d_name,strlen(entry->d_name)));
        closedir(d);nlr_pop();return list;
    }
    if(op==2) {
        struct stat st;if(stat(p,&st))mp_raise_OSError(errno);
        mp_obj_t items[10];for(int i=0;i<10;++i)items[i]=MP_OBJ_NEW_SMALL_INT(0);
        items[0]=mp_obj_new_int(st.st_mode);items[6]=mp_obj_new_int_from_ll(st.st_size);
        items[7]=mp_obj_new_int_from_ll(st.st_atime);items[8]=mp_obj_new_int_from_ll(st.st_mtime);items[9]=mp_obj_new_int_from_ll(st.st_ctime);
        return mp_obj_new_tuple(10,items);
    }
    switch(op){case 3:rc=mkdir(p,0777);break;case 4:rc=unlink(p);break;case 5:rc=rmdir(p);break;
    case 6:if(n!=3)mp_raise_TypeError(MP_ERROR_TEXT("destination required"));path(a[2],q);rc=rename(p,q);break;
    case 7:{struct stat st;if(stat(p,&st))mp_raise_OSError(errno);if(!S_ISDIR(st.st_mode))mp_raise_OSError(ENOTDIR);check(solar_os_shell_set_cwd(hw_context,p));break;}default:mp_raise_ValueError(MP_ERROR_TEXT("unknown filesystem operation"));}
    if(rc)mp_raise_OSError(errno);return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(fs_obj,1,3,fs);
#include "power_shutdown.h"
static mp_obj_t shutdown_requested(void) { return mp_obj_new_bool(sk_power_requested()); }
static MP_DEFINE_CONST_FUN_OBJ_0(shutdown_requested_obj,shutdown_requested);
static void put(mp_obj_t module,const char *name,const void *value) {mp_obj_dict_store(MP_OBJ_FROM_PTR(((mp_obj_module_t *)MP_OBJ_TO_PTR(module))->globals),MP_OBJ_NEW_QSTR(qstr_from_str(name)),MP_OBJ_FROM_PTR(value));}
void sk_python_hardware_init(solar_os_context_t *ctx) {
    hw_context=ctx;mp_obj_t module=mp_obj_new_module(qstr_from_str("_solaros_hw"));
    put(module,"open",&hw_open_obj);put(module,"close",&hw_close_obj);put(module,"value",&hw_value_obj);put(module,"transfer",&hw_transfer_obj);
    put(module,"ticks",&ticks_obj);put(module,"delay",&delay_obj);put(module,"epoch",&epoch_obj);put(module,"fs",&fs_obj);
    mp_obj_t root=mp_obj_new_module(qstr_from_str("solaros"));put(root,"hw",MP_OBJ_TO_PTR(module));
    put(root,"shutdown_requested",&shutdown_requested_obj);
}
void sk_python_hardware_destroy(void) {sk_py_hw_reset();hw_context=NULL;}
#endif
