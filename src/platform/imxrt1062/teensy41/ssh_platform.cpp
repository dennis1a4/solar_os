#if SK_SSH
#include <arduino_freertos.h>
#include "network_socket.h"
#include <errno.h>
#include <sys/select.h>
extern "C" {
#include "solar_os_memory.h"
#include "solar_os_task.h"
#include "solar_os_net_transport.h"
#include "solar_os_identity.h"
#include "mbedtls/entropy.h"
}

extern "C" void *sk_crypto_calloc(size_t n,size_t size) {
    return solar_os_memory_calloc(n,size,SOLAR_OS_MEMORY_TRANSIENT,"crypto");
}
extern "C" void sk_crypto_free(void *p) { solar_os_memory_free(p); }
extern "C" int mbedtls_hardware_poll(void *,unsigned char *out,size_t size,size_t *written) {
    *written=0;
    const uint32_t started=millis();
    while (*written<size) {
        sk_net_request q{}; sk_net_reply r;
        q.op=SK_NET_ENTROPY; q.length=min(size-*written,size_t(SK_NET_CHUNK));
        if (sk_net_call(&q,&r)) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        if (!r.value) {
            if (millis()-started>1000) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
            vTaskDelay(1); continue;
        }
        memcpy(out+*written,r.data,r.value); *written+=r.value;
    }
    return 0;
}
extern "C" void esp_fill_random(void *data,size_t size) {
    size_t done;
    configASSERT(mbedtls_hardware_poll(nullptr,(unsigned char *)data,size,&done)==0 && done==size);
}
extern "C" ssize_t recv(int fd,void *data,size_t size,int flags) {
    if (flags) { errno=EINVAL; return -1; }
    return solar_os_net_transport_recv(fd,data,size);
}
extern "C" ssize_t send(int fd,const void *data,size_t size,int flags) {
    if (flags) { errno=EINVAL; return -1; }
    return solar_os_net_transport_send(fd,data,size);
}
// libssh2 always runs nonblocking; its blocking select path is unsupported.
extern "C" int select(int,fd_set *,fd_set *,fd_set *,timeval *) { errno=ENOSYS; return -1; }
#if !SK_SETTINGS
extern "C" void solar_os_identity_get_user(char *out,size_t size) { strlcpy(out,"user",size); }
extern "C" void solar_os_identity_get_hostname(char *out,size_t size) { strlcpy(out,"teensy41",size); }
#endif

// One foreground worker, with internal OCRAM stack. Retain a terminated worker
// suspended until its owner reaps it, avoiding reuse before FreeRTOS cleanup.
DMAMEM static StackType_t worker_stack[7168];
static StaticTask_t worker_tcb;
static TaskHandle_t worker;
#if SK_BACKGROUND_JOBS
DMAMEM static StackType_t process_stack[10240];
static StaticTask_t process_tcb;
static TaskHandle_t process_worker;
#endif
static void reap() {
    if (worker && eTaskGetState(worker)==eSuspended) { vTaskDelete(worker); worker=nullptr; }
}
extern "C" bool solar_os_task_admit(const char *,uint32_t bytes,solar_os_task_role_t role,bool external) {
#if SK_BACKGROUND_JOBS
    if(role==SOLAR_OS_TASK_ROLE_BACKGROUND)return !external && bytes<=sizeof(process_stack) && !process_worker;
#endif
    return !bytes || (!external && bytes<=sizeof(worker_stack) &&
        (!worker || eTaskGetState(worker)==eSuspended));
}
extern "C" BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t fn,const char *name,
    uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t,solar_os_task_role_t role) {
    reap();
#if SK_BACKGROUND_JOBS
    if(role==SOLAR_OS_TASK_ROLE_BACKGROUND) {
        if(process_worker || !out || !bytes || bytes>sizeof(process_stack))return pdFAIL;
        process_worker=xTaskCreateStatic(fn,name,bytes/sizeof(StackType_t),arg,1,process_stack,&process_tcb);
        *out=process_worker;return process_worker?pdPASS:pdFAIL;
    }
#endif
    if (worker || !bytes || bytes>sizeof(worker_stack) || !out) return pdFAIL;
    // App priorities originate on ESP. On this single-core port the USB
    // console and Ethernet owner run at priority 2: foreground workers must
    // stay below them so a CPU/storage-bound operation can still be cancelled.
    const UBaseType_t worker_priority = min(priority, UBaseType_t(1));
    worker=xTaskCreateStatic(fn,name,bytes/sizeof(StackType_t),arg,worker_priority,worker_stack,&worker_tcb);
    *out=worker; return worker ? pdPASS : pdFAIL;
}
extern "C" void solar_os_task_delete_internal(TaskHandle_t task) {
    configASSERT(!task || task==xTaskGetCurrentTaskHandle());
    for (;;) vTaskSuspend(nullptr);
}
extern "C" bool solar_os_task_wait_done(TaskHandle_t task,volatile bool *done,uint32_t timeout) {
    if (!task) return true;
    uint32_t started=millis();
    while (!*done || eTaskGetState(task)!=eSuspended) {
        if (millis()-started>=timeout) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#if SK_BACKGROUND_JOBS
    if(task==process_worker){vTaskDelete(process_worker);process_worker=nullptr;return true;}
#endif
    reap(); return true;
}
#endif
