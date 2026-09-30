#if SK_PLAYGROUND
#include <arduino_freertos.h>
#include <errno.h>
#include <time.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
}
#if SK_CLOCK
extern "C" uint32_t sk_clock_rtc_epoch() { return Teensy3Clock.get(); }
extern "C" void sk_clock_rtc_set(uint32_t epoch) { Teensy3Clock.set(epoch); }
#endif
extern "C" void solar_os_shell_cmd_rtc(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc==3 && !strcmp(argv[1],"set")) {
        char *end;errno=0;unsigned long long value=strtoull(argv[2],&end,10);
        if(!*argv[2] || *end || errno || value<946684800 || value>UINT32_MAX) {
            solar_os_shell_io_writeln(io,"rtc: expected UTC Unix seconds for 2000..2106");return;
        }
        Teensy3Clock.set(static_cast<uint32_t>(value));
    } else if(argc!=1) {
        solar_os_shell_io_writeln(io,"usage: rtc | rtc set <UTC Unix seconds>");return;
    }
    time_t now=Teensy3Clock.get();struct tm utc{};gmtime_r(&now,&utc);
    solar_os_shell_io_printf(io,"RTC UTC %04d-%02d-%02d %02d:%02d:%02d epoch=%llu\n",
        utc.tm_year+1900,utc.tm_mon+1,utc.tm_mday,utc.tm_hour,utc.tm_min,utc.tm_sec,
        static_cast<unsigned long long>(now));
}
#endif
