#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#define SK_MOTOR_MODULE 1
#include "../../src/platform/imxrt1062/teensy41/motor_module.cpp"
uint32_t test_clock, test_primask;
static bool shutdown_pending, read_fail, wrong_identity, foreign_claim, fail_write, wrong_sequence;
static unsigned write_count;
static bool claimed[3];
static uint8_t device[3][16];
static std::string output;
extern "C" bool sk_power_requested(){return shutdown_pending;}
extern "C" esp_err_t solar_os_resource_claim(solar_os_resource_kind_t kind,int bus,int addr,const char *,const char *) {
    assert(kind==SOLAR_OS_RESOURCE_I2C_ADDRESS && addr>=0x10 && addr<=0x1f);
    if(foreign_claim || claimed[bus])return ESP_FAIL;
    claimed[bus]=true;return ESP_OK;
}
extern "C" esp_err_t solar_os_resource_release(solar_os_resource_kind_t,int bus,int,const char *) {
    assert(claimed[bus]);claimed[bus]=false;return ESP_OK;
}
extern "C" void solar_os_shell_io_writeln(solar_os_shell_io_t *,const char *s){output+=s;output+='\n';}
extern "C" void solar_os_shell_io_printf(solar_os_shell_io_t *,const char *fmt,...) {
    char text[512];va_list args;va_start(args,fmt);vsnprintf(text,sizeof(text),fmt,args);va_end(args);output+=text;
}
esp_err_t sk_i2c_transfer(unsigned bus,uint8_t address,const uint8_t *tx,size_t n,uint8_t *rx,size_t count) {
    assert(bus==1 || bus==2);assert(claimed[bus]);auto *d=device[bus];
    assert(address==d[12]);
    if(n) {
        assert(n==3 && !count);++write_count;
        if(fail_write)return ESP_FAIL;
        if(!wrong_sequence)++d[6];d[5]=0;
        switch(tx[0]) {
        case 0xe4:assert(!tx[1] && !tx[2]);d[4]=0;d[8]=d[9]=d[11]=0;break;
        case 0xe2:assert(tx[1]==1 && tx[2]==0xa5);d[4]=8;break;
        case 0xe3:assert(tx[1]==0xa5 && tx[2]==0x5a);if(!(d[4]&8))d[5]=5;break;
        case 0x82:d[8]=tx[1];d[9]=tx[2];break;
        case 0xaa:d[10]=tx[1];break;
        case 0x1a:assert(tx[1]<=1 && tx[2]);break;
        case 0x1c:d[11]=tx[1];break;
        default:assert(false);
        }
    } else {
        assert(count==16);if(read_fail)return ESP_FAIL;
        memcpy(rx,d,16);if(wrong_identity)rx[0]=0;
    }
    return ESP_OK;
}
static bool shell(std::initializer_list<const char *> args) {
    solar_os_shell_io_t io{};solar_os_context_t ctx{};ctx.io=&io;
    std::vector<char *> argv;for(auto a:args)argv.push_back(const_cast<char *>(a));
    output.clear();sk_shell_cmd_motor(&ctx,argv.size(),argv.data());return io.command_status==0;
}
int main() {
    for(unsigned bus=1;bus<=2;bus++){auto *d=device[bus];d[0]='S';d[1]='K';d[2]=1;d[3]=1;d[12]=0x10;d[13]=12;}
    foreign_claim=true;assert(!shell({"motor","attach","slot0"}));assert(!claimed[2]);foreign_claim=false;
    wrong_identity=true;assert(!shell({"motor","attach","slot0"}));assert(!claimed[2] && !write_count);wrong_identity=false;
    assert(shell({"motor","attach","slot0"}));assert(claimed[2] && !(device[2][4]&8));
    assert(shell({"motor","attach","slot1"}));assert(claimed[1]);
    assert(!shell({"motor","attach","slot2"}));
    assert(!shell({"motor","drive","slot0","100","-80","1200"}));
    device[2][6]=255; // Sequence wraps modulo 256 across stop/arm.
    assert(shell({"motor","arm","slot0"}));
    assert(shell({"motor","drive","slot0","100","-80","1200"}));
    assert(device[2][8]==100 && device[2][9]==80 && device[2][10]==9);
    assert(!shell({"motor","drive","slot0","200","200","60000"}));
    unsigned writes=write_count;test_clock=499;poll();assert(write_count==writes);
    test_clock=500;poll();assert(write_count==writes+1);
    test_clock=1200;poll();assert(!motors[0].moving && !(device[2][4]&8));
    writes=write_count;test_clock=5000;poll();assert(write_count==writes);
    // A lost command response is not replayed; the move is retired and requires arm.
    assert(shell({"motor","arm","slot1"}));fail_write=true;
    assert(!shell({"motor","drive","slot1","60","60","1000"}));assert(!motors[1].moving && motors[1].failed);fail_write=false;
    assert(!shell({"motor","drive","slot1","60","60","1000"}));
    assert(shell({"motor","arm","slot1"}));assert(shell({"motor","step","slot1","1","10","25"}));
    device[1][11]=0;poll();assert(!motors[1].moving && !(device[1][4]&8));
    // Wraparound and reset detection.
    test_clock=0xfffffff0;assert(shell({"motor","arm","slot0"}));
    assert(shell({"motor","drive","slot0","1","1","30"}));test_clock=15;poll();assert(!motors[0].moving);
    assert(shell({"motor","arm","slot0"}));assert(shell({"motor","drive","slot0","1","1","1000"}));
    device[2][4]=0;poll();assert(!motors[0].moving && motors[0].failed);
    wrong_sequence=true;assert(!shell({"motor","arm","slot0"}));wrong_sequence=false;
    assert(shell({"motor","arm","slot0"}));assert(shell({"motor","drive","slot0","1","1","1000"}));
    shutdown_pending=true;poll();assert(!motors[0].moving);assert(!shell({"motor","arm","slot0"}));shutdown_pending=false;
    read_fail=true;assert(!sk_motor_shutdown());assert(!shell({"motor","detach","slot0"}));assert(claimed[2]);read_fail=false;
    assert(sk_motor_shutdown());assert(shell({"motor","detach","slot0"}));assert(shell({"motor","detach","slot1"}));
    assert(!claimed[1] && !claimed[2]);
    puts("PASS: both existing slot buses, claims, identity/sequence, arm, bounded motion, heartbeat, failure, wrap and shutdown");
}
