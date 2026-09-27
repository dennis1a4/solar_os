#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
ssize_t recv(int, void *, size_t, int);
ssize_t send(int, const void *, size_t, int);
#ifdef __cplusplus
}
#endif
