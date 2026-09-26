#pragma once
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sk_directory DIR;
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
struct dirent { unsigned char d_type; char d_name[256]; };
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
#ifdef __cplusplus
}
#endif
