#pragma once
#include "freertos/task.h"
typedef int solar_os_task_role_t;
BaseType_t solar_os_task_create_pinned_external(TaskFunction_t,const char *,uint32_t,void *,UBaseType_t,TaskHandle_t *,BaseType_t,solar_os_task_role_t);
void solar_os_task_delete_external(TaskHandle_t);
void solar_os_task_delete_internal(TaskHandle_t);
