#pragma once
#include <FreeRTOS.h>
#include <task.h>
/* Single-core task critical sections; these call sites are not ISR APIs. */
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(mux) do { (void)(mux); vPortEnterCritical(); } while (0)
#define portEXIT_CRITICAL(mux) do { (void)(mux); vPortExitCritical(); } while (0)
