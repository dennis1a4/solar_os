#pragma once
#include <algorithm>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstddef>
using std::min;
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
inline size_t strlcpy(char *d,const char *s,size_t n){size_t k=strlen(s);if(n){size_t m=k<n-1?k:n-1;memcpy(d,s,m);d[m]=0;}return k;}
