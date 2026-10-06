#if SK_SSH
#include <arduino_freertos.h>
#include "network_socket.h"
#include <errno.h>
#include <sys/select.h>
extern "C" {
#include "solar_os_memory.h"
#include "solar_os_log.h"
#include "solar_os_task.h"
#include "solar_os_net_transport.h"
#include "solar_os_identity.h"
#include "mbedtls/entropy.h"
#include "psa/crypto.h"
}

extern "C" int sk_ssh_crypto_check() {
    const psa_status_t status=psa_crypto_init();
    if(status!=PSA_SUCCESS)return status;
    unsigned char probe[16];
    return psa_generate_random(probe,sizeof(probe));
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
        int error=sk_net_call(&q,&r);
        if (error) {
            SOLAR_OS_LOGW("entropy","RPC error=%d collected=%u requested=%u",error,(unsigned)*written,(unsigned)size);
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        if (!r.value) {
            if (millis()-started>1000) {
                SOLAR_OS_LOGW("entropy","timeout collected=%u requested=%u MCTL=%08lx SDCTL=%08lx FRQ=%08lx CLK=%08lx",(unsigned)*written,(unsigned)size,
                    (unsigned long)TRNG_MCTL,(unsigned long)TRNG_SDCTL,(unsigned long)TRNG_FRQCNT,(unsigned long)CCM_CCGR6);
                return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
            }
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
static constexpr uint32_t worker_stack_limit=7168*sizeof(StackType_t);
static StackType_t *worker_stack;
static StaticTask_t worker_tcb;
static TaskHandle_t worker;
static volatile bool worker_finished;
#if SK_BACKGROUND_JOBS
static constexpr uint32_t process_stack_limit=10240*sizeof(StackType_t);
static StackType_t *process_stack;
static StaticTask_t process_tcb;
static TaskHandle_t process_worker;
#endif
struct ExternalTask {
    StaticTask_t tcb;
    StackType_t *stack;
    TaskHandle_t task;
    bool reserved;
};
static ExternalTask external_tasks[3];
extern "C" BaseType_t solar_os_task_create_pinned_external(TaskFunction_t fn,const char *name,
    uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t,solar_os_task_role_t) {
    if(!fn || !out || bytes<1024 || bytes>32768 || bytes%sizeof(StackType_t))return pdFAIL;
    ExternalTask *entry=nullptr;
    taskENTER_CRITICAL();
    for(auto &slot:external_tasks) if(!slot.reserved){slot.reserved=true;entry=&slot;break;}
    taskEXIT_CRITICAL();
    if(!entry)return pdFAIL;
    entry->stack=static_cast<StackType_t *>(solar_os_memory_alloc(bytes,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM,"worker.stack"));
    if(!entry->stack){entry->reserved=false;return pdFAIL;}
    entry->task=xTaskCreateStatic(fn,name,bytes/sizeof(StackType_t),arg,min(priority,UBaseType_t(1)),entry->stack,&entry->tcb);
    *out=entry->task;
    if(!entry->task){solar_os_memory_free(entry->stack);*entry={};return pdFAIL;}
    return pdPASS;
}
extern "C" void solar_os_task_delete_external(TaskHandle_t task) {
    if(!task)return;
    for(auto &entry:external_tasks)if(entry.task==task){
        configASSERT(eTaskGetState(task)==eSuspended);
        vTaskDelete(task);solar_os_memory_free(entry.stack);entry={};return;
    }
    configASSERT(false);
}
static void reap() {
    if (worker && worker_finished && eTaskGetState(worker)==eSuspended) { vTaskDelete(worker); worker=nullptr; solar_os_memory_free(worker_stack); worker_stack=nullptr; }
}
extern "C" bool solar_os_task_admit(const char *,uint32_t bytes,solar_os_task_role_t role,bool external) {
    if(external){
        if(bytes>32768)return false;
        unsigned available=0;taskENTER_CRITICAL();for(auto &entry:external_tasks)if(!entry.reserved)++available;taskEXIT_CRITICAL();
        solar_os_memory_status_t memory;solar_os_memory_get_status(&memory);
        return available && memory.external.free>bytes+4096;
    }
#if SK_BACKGROUND_JOBS
    if(role==SOLAR_OS_TASK_ROLE_BACKGROUND)return !external && bytes<=process_stack_limit && !process_worker;
#endif
    return !bytes || (!external && bytes<=worker_stack_limit &&
        (!worker || (worker_finished && eTaskGetState(worker)==eSuspended)));
}
extern "C" BaseType_t solar_os_task_create_pinned_internal(TaskFunction_t fn,const char *name,
    uint32_t bytes,void *arg,UBaseType_t priority,TaskHandle_t *out,BaseType_t,solar_os_task_role_t role) {
    reap();
#if SK_BACKGROUND_JOBS
    if(role==SOLAR_OS_TASK_ROLE_BACKGROUND) {
        if(process_worker || !out || !bytes || bytes>process_stack_limit)return pdFAIL;
        process_stack=static_cast<StackType_t *>(solar_os_memory_alloc(bytes,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"python.stack"));
        if(!process_stack)return pdFAIL;
        process_worker=xTaskCreateStatic(fn,name,bytes/sizeof(StackType_t),arg,1,process_stack,&process_tcb);
        if(!process_worker){solar_os_memory_free(process_stack);process_stack=nullptr;}
        *out=process_worker;return process_worker?pdPASS:pdFAIL;
    }
#endif
    if (worker || !bytes || bytes>worker_stack_limit || !out) return pdFAIL;
    worker_finished=false;
    // App priorities originate on ESP. On this single-core port the USB
    // console and Ethernet owner run at priority 2: foreground workers must
    // stay below them so a CPU/storage-bound operation can still be cancelled.
    const UBaseType_t worker_priority = min(priority, UBaseType_t(1));
    worker_stack=static_cast<StackType_t *>(solar_os_memory_alloc(bytes,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"foreground.stack"));
    if(!worker_stack)return pdFAIL;
    worker=xTaskCreateStatic(fn,name,bytes/sizeof(StackType_t),arg,worker_priority,worker_stack,&worker_tcb);
    if(!worker){solar_os_memory_free(worker_stack);worker_stack=nullptr;}
    *out=worker; return worker ? pdPASS : pdFAIL;
}
extern "C" void solar_os_task_delete_internal(TaskHandle_t task) {
    configASSERT(!task || task==xTaskGetCurrentTaskHandle());
    if(xTaskGetCurrentTaskHandle()==worker)worker_finished=true;
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
    if(task==process_worker){vTaskDelete(process_worker);process_worker=nullptr;solar_os_memory_free(process_stack);process_stack=nullptr;return true;}
#endif
    for(auto &entry:external_tasks)if(entry.task==task)return true;
    reap(); return true;
}
#endif
