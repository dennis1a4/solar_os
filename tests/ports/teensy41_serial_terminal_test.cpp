#include <deque>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include "serial_terminal.cpp"
static std::deque<uint8_t> incoming[3];
static bool claimed[3],fail_alloc=false;
static uint32_t rates[3]={115200,115200,115200};
static size_t allocated=0;
static unsigned blocked_writes=0;
static std::vector<uint8_t> transmitted;
extern "C" size_t strlcpy(char *out,const char *in,size_t size){size_t n=strlen(in);if(size){size_t copy=std::min(n,size-1);memcpy(out,in,copy);out[copy]=0;}return n;}
extern "C" void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t,const char *){if(fail_alloc)return nullptr;auto *p=(size_t *)malloc(n+sizeof(size_t));*p=n;allocated+=n;return p+1;}
extern "C" void solar_os_memory_free(void *p){if(p){auto *h=(size_t *)p-1;allocated-=*h;free(h);}}
esp_err_t sk_uart_claim_format(unsigned i,const char *,uint32_t baud,uint16_t){if(i==2 || claimed[i])return ESP_ERR_INVALID_STATE;claimed[i]=true;rates[i]=baud;return ESP_OK;}
esp_err_t sk_uart_release(unsigned i,const char *){assert(claimed[i]);claimed[i]=false;return ESP_OK;}
extern "C" bool solar_os_uart_get_bus_status(const char *s,solar_os_uart_status_t *out){int i=index_of(s);if(i<0)return false;out->baud_rate=rates[i];return true;}
extern "C" esp_err_t solar_os_bus_uart_read(const char *s,uint8_t *out,size_t cap,uint32_t,size_t *n){assert(cap<=128);int i=index_of(s);assert(claimed[i]);*n=0;while(*n<cap && !incoming[i].empty()){out[(*n)++]=incoming[i].front();incoming[i].pop_front();}return ESP_OK;}
extern "C" esp_err_t solar_os_bus_uart_write(const char *s,const uint8_t *data,size_t len,size_t *n){assert(claimed[index_of(s)]);if(blocked_writes){--blocked_writes;*n=0;return ESP_ERR_TIMEOUT;}*n=std::min(len,size_t(3));transmitted.insert(transmitted.end(),data,data+*n);return *n==len?ESP_OK:ESP_ERR_TIMEOUT;}
static std::string contents(const char *p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
    assert(argc==2);std::string base=argv[1],raw=base+"/raw.bin",timed=base+"/timed.txt",blocked=base+"/blocked.bin";
    sk_serial_init();
    uint16_t format=0;
    for(const char *f:{"8N1","8N2","7E1","7E2","7O1","7O2","8E1","8E2","8O1","8O2"})assert(parse_format(f,format));
    assert(parse_format("7E2",format) && format==0x102);
    for(const char *f:{"7N1","5N1","6N1","8M1","8S1","8N1.5","9N1","8N3"})assert(!parse_format(f,format));
    // Full and wrapped ring preserve order and never overwrite queued bytes.
    Ring r;assert(allocate(r,4));uint8_t in[]={0,1,2,3,4,5},out[8];
    assert(r.put(in,6)==4);assert(r.get(out,2)==2 && out[1]==1);
    assert(r.put(in+4,2)==2);assert(r.get(out,8)==4 && out[0]==2 && out[3]==5);release(r);
    fail_alloc=true;assert(sk_serial_attach("uart8",0)==ESP_ERR_NO_MEM && !claimed[1]);fail_alloc=false;
    assert(start_log(2,9600,blocked.c_str(),false)==ESP_ERR_INVALID_STATE && access(blocked.c_str(),F_OK)!=0);
    assert(start_log(1,9600,raw.c_str(),false)==ESP_OK);
    assert(sk_serial_attach("uart8",115200)==ESP_ERR_INVALID_STATE);
    assert(sk_serial_attach("uart8",9600)==ESP_OK);
    assert(sk_serial_attach("uart8",0)==ESP_ERR_INVALID_STATE);
    for(unsigned n=0;n<70000;++n)incoming[1].push_back(n%256);
    while(!incoming[1].empty())capture_once();
    assert(channels[1].rx==70000 && channels[1].log.used==65536);
    assert(channels[1].log_dropped==4464 && channels[1].view_dropped==65904);
    size_t n=0;assert(sk_serial_read("uart8",out,8,&n)==ESP_OK && n==8 && out[7]==7);
    assert(sk_serial_write("uart8",in,6,&n)==ESP_ERR_TIMEOUT && n==3 && channels[1].tx==3);
    sk_serial_detach("uart8");assert(claimed[1]); // recording survives terminal exit
    assert(stop_log(1)==ESP_OK && !claimed[1] && allocated==0);
    auto data=contents(raw.c_str());assert(data.size()==65536);for(size_t j=0;j<data.size();++j)assert(uint8_t(data[j])==j%256);
    assert(start_log(1,9600,raw.c_str(),false)!=ESP_OK);assert(contents(raw.c_str())==data);
    assert(start_log(1,9600,timed.c_str(),true)==ESP_OK);
    assert(sk_serial_attach("uart8",0)==ESP_OK);
    incoming[1]={0,255,13,10};capture_once();
    assert(sk_serial_write("uart8",in,6,&n)==ESP_ERR_TIMEOUT && n==3);
    assert(stop_log(1)==ESP_OK && claimed[1]); // stopping log preserves terminal
    assert(contents(timed.c_str())=="0 RX 00ff0d0a\n0 TX 000102\n");
    sk_serial_detach("uart8");assert(allocated==0 && !claimed[1]);
    std::string flow=base+"/flow.bin";
    settings[1].software_flow=true;
    assert(start_log(1,115200,flow.c_str(),false)==ESP_OK);
    incoming[1]={0x13};capture_once();
    assert(sk_serial_write("uart8",in,2,&n)==ESP_ERR_TIMEOUT && n==0);
    incoming[1]={0x11};capture_once();
    assert(sk_serial_write("uart8",in,2,&n)==ESP_OK && n==2);
    for(unsigned j=0;j<50000;++j)incoming[1].push_back(0x42);
    while(!incoming[1].empty())capture_once();
    assert(channels[1].rx_paused && transmitted.back()==0x13);
    std::vector<uint8_t> drained(20000);assert(channels[1].log.get(drained.data(),drained.size())==drained.size());
    update_flow(1);assert(!channels[1].rx_paused && transmitted.back()==0x11);
    // Control bytes remain in the raw capture even though the terminal consumes them.
    assert(drained[0]==0x13 && drained[1]==0x11);
    assert(stop_log(1)==ESP_OK && allocated==0);
    assert(sk_serial_attach("uart8",0)==ESP_OK);
    channels[1].rx_paused=true;blocked_writes=2;
    sk_serial_detach("uart8");assert(blocked_writes==0 && transmitted.back()==0x11 && allocated==0);
    settings[1].software_flow=false;
    std::string a=base+"/a.bin",b=base+"/b.bin";
    assert(start_log(0,9600,a.c_str(),false)==ESP_OK);
    assert(start_log(1,19200,b.c_str(),false)==ESP_OK);
    incoming[0]={1,2};incoming[1]={3,4};capture_once();
    assert(sk_serial_shutdown()==ESP_OK && !claimed[0] && !claimed[1]);
    assert(contents(a.c_str())==std::string("\1\2",2) && contents(b.c_str())==std::string("\3\4",2));
    assert(sk_serial_shutdown()==ESP_OK && allocated==0);
    assert(start_log(0,9600,(base+"/retry-a.bin").c_str(),false)==ESP_OK);
    assert(start_log(1,19200,(base+"/retry-b.bin").c_str(),false)==ESP_OK);
    assert(stop_log(0)==ESP_OK && !claimed[0] && claimed[1]);
    incoming[1]={5};capture_once();assert(stop_log(1)==ESP_OK);
    assert(contents(a.c_str())==std::string("\1\2",2));
    assert(contents(b.c_str())==std::string("\3\4",2));
    assert(contents((base+"/retry-b.bin").c_str())==std::string("\5",1));
    assert(allocated==0);
    // Partial storage writes latch an error and stop enqueueing.
    auto &c=channels[0];c.file=fopen("/dev/full","wb");assert(c.file);setvbuf(c.file,nullptr,_IONBF,0);
    c.error=ESP_OK;write_chunk(c,in,6);assert(c.error==ESP_FAIL && c.storage_lost==6);enqueue(c,in,6,false);assert(c.log_dropped==6);fclose(c.file);c.file=nullptr;
    for(int j=0;j<100;++j){assert(sk_serial_attach("uart7",0)==ESP_OK);sk_serial_detach("uart7");assert(allocated==0);}
    puts("PASS: serial capture, binary fidelity, overflow, timestamps, partial TX, storage failure, ownership, rollback and cleanup");
}
