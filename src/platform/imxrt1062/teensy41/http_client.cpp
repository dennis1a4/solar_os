// Bounded one-shot GET transport for Playground. All crypto buffers use PSRAM.
#if SK_PLAYGROUND
#include <arduino_freertos.h>
#include <errno.h>
#include <strings.h>
extern "C" {
#include "solar_os_http_client.h"
#include "solar_os_memory.h"
#include "solar_os_net_transport.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "psa/crypto.h"
}
extern const char sk_tls_roots[];
// Opt-in diagnostics must not leak local-session downloads into the USB shell.
#if SK_HTTP_DIAGNOSTICS
extern void sk_console_printf(const char *,...);
#define HTTP_TRACE(...) sk_console_printf(__VA_ARGS__)
#else
#define HTTP_TRACE(...) ((void)0)
#endif
static bool tls_failed(const char *stage,int result) {
    (void)stage;
    if(result)HTTP_TRACE("HTTPS %s: %d\r\n",stage,result);
    return result!=0;
}
extern "C" mbedtls_ms_time_t mbedtls_ms_time() {
    static uint32_t previous; static uint64_t high;
    taskENTER_CRITICAL();
    uint32_t now=millis(); if(now<previous) high+=UINT64_C(1)<<32; previous=now;
    uint64_t result=high+now; taskEXIT_CRITICAL(); return result;
}
extern "C" time_t sk_tls_time(time_t *out) { time_t t=Teensy3Clock.get(); if(out)*out=t; return t; }
struct solar_os_http_request {
    solar_os_http_request_options_t options;
    volatile bool cancel,active;
    bool used;
    int fd=-1;
    uint32_t started;
    solar_os_http_response_t *response;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt roots;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    uint8_t input[2048];
    size_t pos,count;
    bool tls;
};
static bool stopped(void *p) {
    auto *r=static_cast<solar_os_http_request *>(p);
    if(r->cancel || (r->options.cancel_flag && *r->options.cancel_flag) ||
        (r->options.should_cancel && r->options.should_cancel(r->options.cancel_user_data))) {
        r->response->cancelled=true; return true;
    }
    if(r->options.deadline_ms && millis()-r->started>=r->options.deadline_ms) {
        r->response->deadline_exceeded=true; return true;
    }
    return false;
}
static int tls_send(void *p,const unsigned char *buf,size_t n) {
    auto *r=static_cast<solar_os_http_request *>(p);
    int ret=solar_os_net_transport_send(r->fd,buf,n);
    return ret<0 && (errno==EAGAIN || errno==EWOULDBLOCK)?MBEDTLS_ERR_SSL_WANT_WRITE:ret;
}
static int tls_recv(void *p,unsigned char *buf,size_t n) {
    auto *r=static_cast<solar_os_http_request *>(p);
    int ret=solar_os_net_transport_recv(r->fd,buf,n);
    return ret<0 && (errno==EAGAIN || errno==EWOULDBLOCK)?MBEDTLS_ERR_SSL_WANT_READ:ret;
}
static bool again(int n) { return n==MBEDTLS_ERR_SSL_WANT_READ || n==MBEDTLS_ERR_SSL_WANT_WRITE; }
static uint32_t timeout(solar_os_http_request *r) { return r->options.timeout_ms?r->options.timeout_ms:15000; }
static int transfer(solar_os_http_request *r,void *buf,size_t n,bool write) {
    uint32_t start=millis();
    do {
        if(stopped(r)) return -1;
        int got=r->tls?(write?mbedtls_ssl_write(&r->ssl,(const unsigned char *)buf,n):
            mbedtls_ssl_read(&r->ssl,(unsigned char *)buf,n)):
            (write?tls_send(r,(const unsigned char *)buf,n):tls_recv(r,(unsigned char *)buf,n));
        if(got==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)return 0;
        if(!again(got))return got;
        vTaskDelay(1);
    } while(millis()-start<timeout(r));
    return -1;
}
static bool send_text(solar_os_http_request *r,const char *s) {
    size_t n=strlen(s);
    while(n) { int sent=transfer(r,(void *)s,n,true); if(sent<=0)return false; s+=sent;n-=sent; }
    return true;
}
static int read_byte(solar_os_http_request *r) {
    if(r->pos==r->count) {
        int n=transfer(r,r->input,sizeof(r->input),false);
        if(n<=0)return n==0?-1:-2;
        r->pos=0;r->count=n;
    }
    return r->input[r->pos++];
}
static bool line(solar_os_http_request *r,char *out,size_t len) {
    size_t n=0;
    for(;;) { int c=read_byte(r);if(c<0 || n+1>=len)return false;
        if(c=='\n') { if(n && out[n-1]=='\r')--n;out[n]=0;return true; } out[n++]=c;
    }
}
static esp_err_t event(solar_os_http_request *r,solar_os_http_event_type_t type,
    const char *key=nullptr,const char *value=nullptr,const uint8_t *data=nullptr,size_t size=0) {
    solar_os_http_event_t e{};e.type=type;e.status_code=r->response->status_code;
    e.content_length=r->response->content_length;e.header_name=key;e.header_value=value;e.data=data;e.data_len=size;
    return r->options.event_handler?r->options.event_handler(&e,r->options.user_data):ESP_OK;
}
static void disconnect(solar_os_http_request *r) {
    if(r->fd>=0)solar_os_net_transport_close(r->fd);
    r->fd=-1;
    mbedtls_ssl_free(&r->ssl);mbedtls_ssl_config_free(&r->config);mbedtls_x509_crt_free(&r->roots);
    mbedtls_ctr_drbg_free(&r->rng);mbedtls_entropy_free(&r->entropy);
}
static esp_err_t perform(solar_os_http_request *r) {
    char url[768]; if(strlcpy(url,r->options.url,sizeof(url))>=sizeof(url))return ESP_ERR_INVALID_SIZE;
    const unsigned max_redirects=r->options.follow_redirects?(r->options.max_redirects?r->options.max_redirects:5):0;
    for(unsigned redirects=0;;++redirects) {
        r->pos=r->count=0;
        r->tls=!strncmp(url,"https://",8);
        if(!r->tls && strncmp(url,"http://",7))return ESP_ERR_INVALID_ARG;
        char host[254],path[768];const char *start=url+(r->tls?8:7),*slash=strchr(start,'/');
        size_t len=slash?size_t(slash-start):strlen(start);
        if(!len || len>=sizeof(host))return ESP_ERR_INVALID_ARG;
        memcpy(host,start,len);host[len]=0;strlcpy(path,slash?slash:"/",sizeof(path));
        if(strpbrk(host,"@\r\n") || strpbrk(path,"\r\n"))return ESP_ERR_INVALID_ARG;
        unsigned port=r->tls?443:80;char *colon=strchr(host,':');
        if(colon) { *colon++=0;char *end;port=strtoul(colon,&end,10);if(*end || !port || port>65535)return ESP_ERR_INVALID_ARG; }
        char ip[32];if(solar_os_net_transport_resolve(host,ip,sizeof(ip),timeout(r),stopped,r))return ESP_FAIL;
        r->fd=solar_os_net_transport_open(false,0);if(r->fd<0)return ESP_FAIL;
        if(solar_os_net_transport_connect(r->fd,ip,port)<0 && errno!=EINPROGRESS)return ESP_FAIL;
        uint32_t connect_start=millis();
        for(;;) { if(stopped(r))return ESP_ERR_TIMEOUT;int ready=solar_os_net_transport_wait(r->fd,true,10);
            if(ready>0)break;
            if(ready<0 || millis()-connect_start>=timeout(r))return ESP_ERR_TIMEOUT; }
        if(r->tls) {
            mbedtls_ssl_init(&r->ssl);mbedtls_ssl_config_init(&r->config);mbedtls_x509_crt_init(&r->roots);
            mbedtls_entropy_init(&r->entropy);mbedtls_ctr_drbg_init(&r->rng);
            const unsigned char seed[]="SolarOS HTTPS";
            if(tls_failed("PSA init",psa_crypto_init()) ||
                tls_failed("entropy",mbedtls_ctr_drbg_seed(&r->rng,mbedtls_entropy_func,&r->entropy,seed,sizeof(seed))) ||
                tls_failed("roots",mbedtls_x509_crt_parse(&r->roots,(const unsigned char *)sk_tls_roots,strlen(sk_tls_roots)+1)) ||
                tls_failed("config",mbedtls_ssl_config_defaults(&r->config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT)))return ESP_FAIL;
            // Keep key exchange at the broadly supported 128-bit security level;
            // the SSH configuration also enables much larger curves.
            static const uint16_t groups[]={MBEDTLS_SSL_IANA_TLS_GROUP_X25519,MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,0};
            mbedtls_ssl_conf_groups(&r->config,groups);
            mbedtls_ssl_conf_authmode(&r->config,MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_ca_chain(&r->config,&r->roots,nullptr);
            mbedtls_ssl_conf_rng(&r->config,mbedtls_ctr_drbg_random,&r->rng);
            if(tls_failed("setup",mbedtls_ssl_setup(&r->ssl,&r->config)) || tls_failed("hostname",mbedtls_ssl_set_hostname(&r->ssl,host)))return ESP_FAIL;
            mbedtls_ssl_set_bio(&r->ssl,r,tls_send,tls_recv,nullptr);
            uint32_t handshake_start=millis();
            while(!mbedtls_ssl_is_handshake_over(&r->ssl)) {
                #if SK_HTTP_DIAGNOSTICS
                const int state=r->ssl.MBEDTLS_PRIVATE(state);const uint32_t step_start=millis();
#endif
                int ret=mbedtls_ssl_handshake_step(&r->ssl);
                #if SK_HTTP_DIAGNOSTICS
                if(millis()-step_start>50)HTTP_TRACE("TLS step=%d ms=%lu ret=%d errno=%d\r\n",state,(unsigned long)(millis()-step_start),ret,errno);
#endif
                if(ret && !again(ret)) { HTTP_TRACE("HTTPS handshake: -0x%x flags=%lx\r\n",-ret,(unsigned long)mbedtls_ssl_get_verify_result(&r->ssl));return ESP_FAIL; }
                if(stopped(r) || millis()-handshake_start>=timeout(r))return ESP_ERR_TIMEOUT;
                vTaskDelay(1);
            }
        }
        char head[1200];snprintf(head,sizeof(head),"GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: %s\r\nConnection: close\r\nAccept-Encoding: identity\r\n",path,host,port,r->options.user_agent?r->options.user_agent:"SolarOS");
        if(!send_text(r,head))return ESP_FAIL;
        for(size_t i=0;i<r->options.header_count;++i) {
            auto &h=r->options.headers[i];
            if(!h.name || !h.value || strpbrk(h.name,"\r\n:") || strpbrk(h.value,"\r\n"))return ESP_ERR_INVALID_ARG;
            if(!send_text(r,h.name) || !send_text(r,": ") || !send_text(r,h.value) || !send_text(r,"\r\n"))return ESP_FAIL;
        }
        if(!send_text(r,"\r\n") || !line(r,head,sizeof(head)) || sscanf(head,"HTTP/%*u.%*u %d",&r->response->status_code)!=1)return ESP_ERR_INVALID_RESPONSE;
        bool chunked=false;char location[768]={};r->response->content_length=-1;
        size_t header_bytes=0;
        for(;;) {
            if(!line(r,head,sizeof(head)) || (header_bytes+=strlen(head))>16384)return ESP_ERR_INVALID_RESPONSE;
            if(!*head)break;
            char *value=strchr(head,':');if(!value)return ESP_ERR_INVALID_RESPONSE;
            *value++=0;while(*value==' ' || *value=='\t')++value;
            if(!strcasecmp(head,"Content-Length")) {char *end;long long n=strtoll(value,&end,10);if(*end || n<0)return ESP_ERR_INVALID_RESPONSE;r->response->content_length=n;}
            if(!strcasecmp(head,"Transfer-Encoding")) {if(strcasecmp(value,"chunked"))return ESP_ERR_NOT_SUPPORTED;chunked=true;}
            if(!strcasecmp(head,"Location") && strlcpy(location,value,sizeof(location))>=sizeof(location))return ESP_ERR_INVALID_SIZE;
            esp_err_t e=event(r,SOLAR_OS_HTTP_EVENT_HEADER,head,value);if(e!=ESP_OK)return e;
        }
        const int status=r->response->status_code;
        if(status==301 || status==302 || status==303 || status==307 || status==308) {
            if(!r->options.follow_redirects)return event(r,SOLAR_OS_HTTP_EVENT_RESPONSE);
            if(redirects>=max_redirects || !*location)return ESP_ERR_INVALID_RESPONSE;
            if(location[0]=='/' && location[1]!='/') {
                int n=snprintf(url,sizeof(url),"%s://%s:%u%s",r->tls?"https":"http",host,port,location);
                if(n<0 || size_t(n)>=sizeof(url))return ESP_ERR_INVALID_SIZE;
            }
            else { if(r->tls && strncmp(location,"https://",8))return ESP_ERR_NOT_ALLOWED;strlcpy(url,location,sizeof(url)); }
            disconnect(r);continue;
        }
        uint8_t data[2048];uint64_t remaining=r->response->content_length<0?UINT64_MAX:r->response->content_length;
        for(;;) {
            if(chunked) { if(!line(r,head,sizeof(head)))return ESP_FAIL;char *end;remaining=strtoull(head,&end,16);if(end==head || (*end && *end!=';'))return ESP_ERR_INVALID_RESPONSE;
                if(!remaining) { do {if(!line(r,head,sizeof(head)))return ESP_FAIL;}while(*head);break; } }
            while(remaining) {
                size_t got=0;bool eof=false;
                while(got<sizeof(data) && got<remaining) {int c=read_byte(r);if(c<0) {if(c==-2 || chunked || r->response->content_length>=0)return ESP_FAIL;eof=true;break;}data[got++]=c;}
                if(got) {esp_err_t e=event(r,SOLAR_OS_HTTP_EVENT_DATA,nullptr,nullptr,data,got);if(e!=ESP_OK)return e;r->response->bytes_received+=got;remaining-=got;}
                if(eof) {remaining=0;break;}if(stopped(r))return ESP_ERR_TIMEOUT;
            }
            if(!chunked)break;
            if(read_byte(r)!='\r' || read_byte(r)!='\n')return ESP_ERR_INVALID_RESPONSE;
        }
        return event(r,SOLAR_OS_HTTP_EVENT_RESPONSE);
    }
}
extern "C" esp_err_t solar_os_http_request_create(const solar_os_http_request_options_t *options,solar_os_http_request_t **out) {
    if(!out || !options || !options->url || (options->header_count && !options->headers) || options->method!=SOLAR_OS_HTTP_METHOD_GET)return ESP_ERR_INVALID_ARG;
    *out=nullptr;auto *r=(solar_os_http_request *)solar_os_memory_calloc(1,sizeof(solar_os_http_request),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"https");
    if(!r)return ESP_ERR_NO_MEM;
    r->options=*options;r->fd=-1;*out=r;return ESP_OK;
}
extern "C" esp_err_t solar_os_http_request_perform(solar_os_http_request_t *r,solar_os_http_response_t *response) {
    if(!r || !response || r->active || r->used)return ESP_ERR_INVALID_STATE;
    memset(response,0,sizeof(*response));r->response=response;r->active=r->used=true;r->started=millis();
    esp_err_t err=perform(r);
    HTTP_TRACE("HTTP result=%ld status=%d bytes=%llu ms=%lu\r\n",(long)err,response->status_code,(unsigned long long)response->bytes_received,(unsigned long)(millis()-r->started));
    disconnect(r);response->duration_ms=millis()-r->started;r->active=false;return err;
}
extern "C" esp_err_t solar_os_http_request_cancel(solar_os_http_request_t *r) {if(!r)return ESP_ERR_INVALID_ARG;r->cancel=true;return ESP_OK;}
extern "C" esp_err_t solar_os_http_request_destroy(solar_os_http_request_t *r) {if(!r)return ESP_OK;if(r->active)return ESP_ERR_INVALID_STATE;solar_os_memory_free(r);return ESP_OK;}
#endif
