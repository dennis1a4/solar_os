#include <cstdio>
#include "power_shutdown.cpp"
uint32_t test_now;
static int preflight=1,cuts,serial_calls,storage_calls,pd_calls;
static bool ready=true,storage_ok=true,pd_ok=true;
static esp_err_t serial_result=ESP_OK;
static unsigned cleanup_delay;
extern "C" int sk_shutdown_preflight(){return preflight;}
extern "C" bool sk_shutdown_ready(){return ready;}
extern "C" esp_err_t sk_serial_shutdown(){++serial_calls;test_now+=cleanup_delay;return serial_result;}
extern "C" bool sk_storage_shutdown_sync(){++storage_calls;return storage_ok;}
extern "C" bool sk_pd_shutdown(){++pd_calls;return pd_ok;}
extern "C" void sk_power_button_init(){}
extern "C" void sk_power_cut(){++cuts;}
static void reset(){
    pending=cleaning=button_pending=false;check_only=false;test_now=100;
    preflight=1;ready=storage_ok=pd_ok=true;serial_result=ESP_OK;
    cuts=serial_calls=storage_calls=pd_calls=cleanup_delay=0;
}
int main(){
    reset();assert(request(true));assert(!request(false));shutdown_step();
    assert(sk_power_requested() && sk_power_cleaning() && !sk_power_interrupt_due());
    test_now+=1999;assert(!sk_power_interrupt_due());++test_now;assert(sk_power_interrupt_due());
    shutdown_step();assert(!pending && !cuts && serial_calls==1 && storage_calls==1 && pd_calls==1);
    assert(strstr(status,"check complete"));
    reset();sk_power_button_event();shutdown_step();shutdown_step();assert(cuts==1);
    reset();preflight=-1;request(false);shutdown_step();assert(!pending && !cuts && !serial_calls);
    reset();preflight=0;request(false);shutdown_step();assert(pending && !cleaning);
    test_now+=15000;shutdown_step();assert(!pending && !cuts && !serial_calls);
    reset();ready=false;request(false);shutdown_step();test_now+=15000;shutdown_step();assert(!pending && !cuts);
    reset();serial_result=ESP_FAIL;request(false);shutdown_step();shutdown_step();assert(!pending && !cuts && !storage_calls);
    reset();storage_ok=false;request(false);shutdown_step();shutdown_step();assert(!pending && !cuts && !pd_calls);
    reset();pd_ok=false;request(false);shutdown_step();shutdown_step();assert(!pending && !cuts);
    reset();cleanup_delay=15000;request(false);shutdown_step();shutdown_step();assert(!pending && !cuts && strstr(status,"deadline"));
    // Wraparound must preserve both grace and deadline calculations.
    reset();test_now=UINT32_MAX-1000;ready=false;request(false);shutdown_step();test_now+=2000;assert(sk_power_interrupt_due());
    test_now+=13000;shutdown_step();assert(!pending && !cuts);
    // A refused request can be retried; busy presses cannot change dry-run mode.
    reset();request(true);sk_power_button_event();shutdown_step();shutdown_step();assert(!cuts && !pending);
    request(false);shutdown_step();shutdown_step();assert(cuts==1);
    puts("shutdown coordinator: PASS");
}
