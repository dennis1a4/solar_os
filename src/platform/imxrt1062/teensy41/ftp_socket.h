#pragma once
/* Private FTP facade: descriptors never enter libc or the SSH transport. */
#include <sys/types.h>
#include <sys/select.h>
#include <stdint.h>
#include <stdbool.h>
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifndef __cplusplus
#include "freertos/portmacro.h"
#endif
#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_RCVTIMEO 3
#define SO_SNDTIMEO 4
#define SHUT_RDWR 2
typedef unsigned socklen_t;
struct sockaddr { uint16_t sa_family; char sa_data[14]; };
struct sockaddr_in { uint16_t sin_family, sin_port; struct in_addr sin_addr; char zero[8]; };
#ifdef __cplusplus
extern "C" {
#endif
#include "solar_os_task.h"
void sk_ftp_set_cancel(int,bool (*)(void *),void *);
int sk_ftp_socket(int,int,int);
int sk_ftp_close(int);
int sk_ftp_shutdown(int,int);
int sk_ftp_setsockopt(int,int,int,const void *,socklen_t);
int sk_ftp_connect(int,const struct sockaddr *,socklen_t);
int sk_ftp_bind(int,const struct sockaddr *,socklen_t);
int sk_ftp_listen(int,int);
int sk_ftp_accept(int,struct sockaddr *,socklen_t *);
int sk_ftp_getsockname(int,struct sockaddr *,socklen_t *);
int sk_ftp_getpeername(int,struct sockaddr *,socklen_t *);
ssize_t sk_ftp_send(int,const void *,size_t,int);
ssize_t sk_ftp_recv(int,void *,size_t,int);
int sk_ftp_select(int,fd_set *,fd_set *,fd_set *,struct timeval *);
int sk_ftp_inet_pton(int,const char *,void *);
uint32_t sk_ftp_random(void);
BaseType_t sk_ftp_task_create(TaskFunction_t,const char *,uint32_t,void *,UBaseType_t,TaskHandle_t *,BaseType_t,solar_os_task_role_t);
void sk_ftp_task_reap(void);
void sk_ftp_task_delete(TaskHandle_t);
#ifdef __cplusplus
}
#endif
#define socket sk_ftp_socket
#define close sk_ftp_close
#define shutdown sk_ftp_shutdown
#define setsockopt sk_ftp_setsockopt
#define connect sk_ftp_connect
#define bind sk_ftp_bind
#define listen sk_ftp_listen
#define accept sk_ftp_accept
#define getsockname sk_ftp_getsockname
#define getpeername sk_ftp_getpeername
#define send sk_ftp_send
#define recv sk_ftp_recv
#define select sk_ftp_select
#define inet_pton sk_ftp_inet_pton
#define esp_random sk_ftp_random
