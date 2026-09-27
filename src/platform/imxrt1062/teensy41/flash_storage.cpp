#if SK_QSPI_FLASH
#include <arduino_freertos.h>
#include <LittleFS.h>
#include "flash_policy.h"
#include "flash_storage.h"
extern "C" {
#include "solar_os_shell_commands.h"
#include "solar_os_shell_io.h"
}
// Use PJRC's fitted-chip driver; expose raw LittleFS calls so POSIX error,
// access-mode, exclusive-create, append and sync semantics are not lost in File.
class SolarFlash : public LittleFS_QSPIFlash {
    using Read = int (*)(const lfs_config *, lfs_block_t, lfs_off_t, void *, lfs_size_t);
    using Prog = int (*)(const lfs_config *, lfs_block_t, lfs_off_t, const void *, lfs_size_t);
    Read driver_read=nullptr;
    Prog driver_prog=nullptr;
    static int staged_read(const lfs_config *c,lfs_block_t b,lfs_off_t o,void *data,lfs_size_t size) {
        auto *self=static_cast<SolarFlash *>(c->context);
        alignas(4) uint8_t staging[256];
        auto *out=static_cast<uint8_t *>(data);
        while (size) {
            lfs_size_t n=size<sizeof(staging) ? size : sizeof(staging);
            // Flash IP transfers and memory-mapped PSRAM share FlexSPI2. Never
            // touch PSRAM or switch tasks while the IP FIFO needs servicing.
            // Bound the critical section to one 256-byte IP read.
            taskENTER_CRITICAL();
            int err=self->driver_read(c,b,o,staging,n);
            taskEXIT_CRITICAL();
            if (err) return err;
            memcpy(out,staging,n); out+=n; o+=n; size-=n;
        }
        return 0;
    }
    static int staged_prog(const lfs_config *c,lfs_block_t b,lfs_off_t o,const void *data,lfs_size_t size) {
        auto *self=static_cast<SolarFlash *>(c->context);
        alignas(4) uint8_t staging[256];
        auto *in=static_cast<const uint8_t *>(data);
        while (size) {
            lfs_size_t n=size<sizeof(staging) ? size : sizeof(staging);
            memcpy(staging,in,n);
            // One page program, including the driver's bounded busy wait.
            // Do not hold this critical section over an erase or a whole file.
            taskENTER_CRITICAL();
            int err=self->driver_prog(c,b,o,staging,n);
            taskEXIT_CRITICAL();
            if (err) return err;
            in+=n; o+=n; size-=n;
        }
        return 0;
    }
public:
    void guard_io(const lfs_config *c) {
        if (c!=&config || config.read==staged_read) return;
        driver_read=config.read; driver_prog=config.prog;
        config.read=staged_read; config.prog=staged_prog;
    }
    bool mount_existing() { return mounted || begin(); }
    bool present() const { return configured; }
    bool is_mounted() const { return mounted; }
    uint32_t capacity() const { return configured ? config.block_size*config.block_count : 0; }
    lfs_t *fs() { return mounted ? &lfs : nullptr; }
    static void cooperate() { vTaskDelay(1); }
    int blank() { return configured ? sk_flash_scan_blank(&config,cooperate) : LFS_ERR_IO; }
    int initialize() {
        if (!configured || mounted) return LFS_ERR_INVAL;
        int err=sk_flash_initialize_blank(&lfs,&config,cooperate);
        if (err) return err;
        err=lfs_mount(&lfs,&config); mounted=err==0; return err;
    }
};
static SolarFlash flash;
extern "C" int __real_lfs_mount(lfs_t *,const lfs_config *);
extern "C" int __wrap_lfs_mount(lfs_t *fs,const lfs_config *config) {
    // Install before begin() attempts its initial mount, then retain the same
    // callbacks for formatting, normal operations and later mount attempts.
    flash.guard_io(config);
    return __real_lfs_mount(fs,config);
}
static unsigned leases;
void sk_flash_begin() { flash.mount_existing(); }
bool sk_flash_mounted() { return flash.is_mounted(); }
lfs_t *sk_flash_fs() { return flash.fs(); }
void sk_flash_acquire() { ++leases; }
void sk_flash_release() { configASSERT(leases); --leases; }
static void status(solar_os_shell_io_t *io) {
    if (!flash.present()) { solar_os_shell_io_writeln(io,"QSPI flash: not detected (no format attempted)"); return; }
    solar_os_shell_io_printf(io,"QSPI flash: %s, %lu bytes, %s at /flash, open=%u\n",
        flash.getMediaName(),(unsigned long)flash.capacity(),
        flash.is_mounted() ? "mounted" : "unmounted",leases);
    if (flash.is_mounted()) solar_os_shell_io_printf(io,"LittleFS: %lu used / %lu bytes\n",
        (unsigned long)flash.usedSize(),(unsigned long)flash.capacity());
}
extern "C" void solar_os_shell_cmd_flash(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if (argc==1 || (argc==2 && !strcmp(argv[1],"status"))) { status(io); return; }
    if (argc==2 && !strcmp(argv[1],"mount")) { flash.mount_existing(); status(io); return; }
    if (argc==2 && !strcmp(argv[1],"scan")) {
        solar_os_shell_io_writeln(io,"Scanning flash without writing...");
        int err=flash.blank();
        solar_os_shell_io_writeln(io,err==0 ? "Flash is completely blank" :
            err==LFS_ERR_EXIST ? "Flash contains data; initialization will be refused" : "Flash read failed or chip unavailable");
        return;
    }
    if (argc==2 && !strcmp(argv[1],"init")) {
        if (flash.is_mounted() || leases) { solar_os_shell_io_writeln(io,"flash init: refused (already mounted or in use)"); return; }
        solar_os_shell_io_writeln(io,"Checking entire chip; initialize only if every byte is erased...");
        int err=flash.initialize();
        if (err) solar_os_shell_io_printf(io,"flash init: refused or failed (%d); nonblank media is never formatted\n",err);
        status(io); return;
    }
    solar_os_shell_io_writeln(io,"usage: flash [status|mount|scan|init] (init accepts only a completely blank chip)");
}
#endif
