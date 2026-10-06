#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
using std::min;
#define SK_BACKGROUND_JOBS 1
#define configASSERT assert
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(x) (x)
using BaseType_t=int;
using UBaseType_t=unsigned;
using StackType_t=uint32_t;
using TaskFunction_t=void (*)(void *);
enum State {eRunning,eSuspended};
struct FakeTask { State state; void *stack; };
using TaskHandle_t=FakeTask *;
struct StaticTask_t { FakeTask task; };
enum solar_os_task_role_t {SOLAR_OS_TASK_ROLE_FOREGROUND,SOLAR_OS_TASK_ROLE_BACKGROUND,SOLAR_OS_TASK_ROLE_SYSTEM};
enum solar_os_memory_class_t {SOLAR_OS_MEMORY_INTERNAL_PREFERRED,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM};
struct solar_os_memory_status_t { struct {size_t free;} external; };
static std::map<void *,size_t> allocations;
static bool alloc_fail,create_fail;
static unsigned now;
static void *solar_os_memory_alloc(size_t size,solar_os_memory_class_t,const char *) {
    if(alloc_fail)return nullptr;
    void *p=malloc(size);assert(p);allocations[p]=size;return p;
}
static void solar_os_memory_free(void *p) {if(p){assert(allocations.erase(p)==1);free(p);}}
static void solar_os_memory_get_status(solar_os_memory_status_t *m) {m->external.free=8*1024*1024;}
static TaskHandle_t xTaskCreateStatic(TaskFunction_t,const char *,size_t words,void *,unsigned,StackType_t *stack,StaticTask_t *tcb) {
    assert(allocations.at(stack)>=words*sizeof(StackType_t));
    if(create_fail)return nullptr;
    tcb->task={eRunning,stack};return &tcb->task;
}
static State eTaskGetState(TaskHandle_t t) {return t->state;}
static void vTaskDelete(TaskHandle_t t) {assert(t->state==eSuspended && allocations.count(t->stack));}
static TaskHandle_t current_task;
struct ParkedTask {};
static void vTaskSuspend(void *) {current_task->state=eSuspended;throw ParkedTask{};}
static TaskHandle_t xTaskGetCurrentTaskHandle() {return current_task;}
static unsigned millis(){return now;}
static void vTaskDelay(unsigned n){now+=n;}
