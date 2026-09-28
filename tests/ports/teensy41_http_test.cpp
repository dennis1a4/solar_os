#include "arduino_freertos.h"
#include <assert.h>
#include <errno.h>
#include <string>
#include <vector>
extern "C" {
#include "solar_os_http_client.h"
#include "solar_os_memory.h"
#include "solar_os_net_transport.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"
}
uint32_t fake_millis;
TestClock Teensy3Clock;
time_t fake_epoch=1790000000;
static size_t allocations;
extern "C" void *solar_os_memory_calloc(size_t n,size_t s,solar_os_memory_class_t,const char *) { ++allocations;return calloc(n,s); }
extern "C" void solar_os_memory_free(void *p) {if(p)--allocations;free(p);}
extern "C" void *sk_crypto_calloc(size_t n,size_t s) { return calloc(n,s); }
extern "C" void sk_crypto_free(void *p) { free(p); }
extern "C" int mbedtls_hardware_poll(void *,unsigned char *out,size_t n,size_t *done) { memset(out,42,n);*done=n;return 0; }
void sk_console_printf(const char *,...) {}
static std::vector<std::string> responses;
static std::string sent,body;
static size_t connection,offset;
static int sockets;
static bool would_block;
extern "C" int solar_os_net_transport_resolve(const char *,char *ip,size_t n,uint32_t,bool (*cancel)(void *),void *p) { if(cancel(p))return -1;snprintf(ip,n,"127.0.0.1");return 0; }
extern "C" int solar_os_net_transport_open(bool,uint16_t) { ++sockets;return 1; }
extern "C" void solar_os_net_transport_close(int) { --sockets;++connection;offset=0; }
extern "C" int solar_os_net_transport_connect(int,const char *,uint16_t) {return 0;}
extern "C" int solar_os_net_transport_wait(int,bool,uint32_t) {return 1;}
extern "C" ssize_t solar_os_net_transport_send(int,const void *p,size_t n) {n=std::min(n,size_t(7));sent.append((const char *)p,n);return n;}
extern "C" ssize_t solar_os_net_transport_recv(int,void *p,size_t n) {
    if(would_block) {errno=EAGAIN;return -1;}
    assert(connection<responses.size());const auto &s=responses[connection];n=std::min(n,std::min(size_t(3),s.size()-offset));
    memcpy(p,s.data()+offset,n);offset+=n;return n;
}
static esp_err_t receive(const solar_os_http_event_t *e,void *) {
    if(e->type==SOLAR_OS_HTTP_EVENT_DATA)body.append((const char *)e->data,e->data_len);
    return ESP_OK;
}
static esp_err_t run(std::vector<std::string> input,solar_os_http_response_t &response,bool cancel=false) {
    responses=input;connection=offset=0;sent.clear();body.clear();fake_millis=0;
    solar_os_http_request_options_t options{};options.url="http://example.test/catalog";options.follow_redirects=true;options.event_handler=receive;options.timeout_ms=10;options.deadline_ms=20;
    solar_os_http_request_t *r;assert(solar_os_http_request_create(&options,&r)==ESP_OK);
    if(cancel)assert(solar_os_http_request_cancel(r)==ESP_OK);
    auto result=solar_os_http_request_perform(r,&response);
    assert(solar_os_http_request_perform(r,&response)==ESP_ERR_INVALID_STATE);
    assert(solar_os_http_request_destroy(r)==ESP_OK);assert(sockets==0 && allocations==0);return result;
}
extern const char sk_tls_roots[];
int main() {
    assert(psa_crypto_init()==PSA_SUCCESS);
    const std::string roots(sk_tls_roots);size_t at=0;
    while((at=roots.find("-----BEGIN CERTIFICATE-----",at))!=std::string::npos) {
        auto end=roots.find("-----END CERTIFICATE-----",at)+25;
        auto pem=roots.substr(at,end-at)+"\n";
        mbedtls_x509_crt root;mbedtls_x509_crt_init(&root);
        int ret=mbedtls_x509_crt_parse(&root,(const unsigned char *)pem.c_str(),pem.size()+1);
        char error[256];mbedtls_strerror(ret,error,sizeof(error));fprintf(stderr,"root %zu: %d %s\n",at,ret,error);
        assert(ret==0);
        uint32_t flags=0;
        assert(mbedtls_x509_crt_verify(&root,&root,nullptr,nullptr,&flags,nullptr,nullptr)==0 && flags==0);
        fake_epoch=4102444800;
        assert(mbedtls_x509_crt_verify(&root,&root,nullptr,nullptr,&flags,nullptr,nullptr)!=0 && (flags&MBEDTLS_X509_BADCERT_EXPIRED));
        fake_epoch=1790000000;
        assert(mbedtls_x509_crt_verify(&root,&root,nullptr,"wrong.invalid",&flags,nullptr,nullptr)!=0 && (flags&MBEDTLS_X509_BADCERT_CN_MISMATCH));
        mbedtls_x509_crt_free(&root);at=end;
    }

    solar_os_http_response_t r;
    assert(run({"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"},r)==ESP_OK && body=="hello" && r.bytes_received==5);
    assert(sent.find("GET /catalog HTTP/1.1\r\n")!=std::string::npos);
    assert(run({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhe\r\n3;extension=yes\r\nllo\r\n0\r\nX-Trailer: value\r\n\r\n"},r)==ESP_OK && body=="hello");
    assert(run({"HTTP/1.1 200 OK\r\n\r\nhello"},r)==ESP_OK && body=="hello");
    assert(run({"HTTP/1.1 302 Found\r\nLocation: /next\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok"},r)==ESP_OK && body=="ok");
    assert(sent.find("GET /next HTTP/1.1")!=std::string::npos);
    assert(run({"HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nshort"},r)!=ESP_OK);
    assert(run({"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n"},r)!=ESP_OK);
    assert(run({"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n"},r)!=ESP_OK);
    assert(run({"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nx\r\n"},r)!=ESP_OK);
    assert(run({"HTTP/1.1 200 OK\r\n"+std::string(2000,'a')+"\r\n\r\n"},r)!=ESP_OK);
    assert(run({},r,true)!=ESP_OK && r.cancelled);
    would_block=true;assert(run({""},r)!=ESP_OK);would_block=false;
    puts("PASS: CA parsing/date/hostname checks; HTTP fragmented reads/writes, lengths, chunking, redirects, malformed input, cancellation, timeout and cleanup");
}
