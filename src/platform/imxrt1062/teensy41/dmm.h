#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Waits until sampling, drawing, pin claims and display ownership are released.
bool sk_dmm_stop(void);
#ifdef __cplusplus
}
#endif
