#if SK_SD_RECOVERY
#include <arduino_freertos.h>
#include "platform.h"
#include "sd_storage.h"
#include "sd_recovery_state.h"
#include "storage_lock.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
}

namespace {
constexpr uint8_t detect_pin = 46; // Teensy 4.1 SD DAT3, as in SDClass::mediaPresent.
constexpr uint32_t probe_ms = 250, retry_ms = 2000;
SdRecoveryState state;
FsVolume volume;
uint32_t next_probe, next_retry, last_mount_ms, mount_attempts, losses;
uint8_t last_error;
bool detect_mode;
uint8_t sector[512] __attribute__((aligned(32))); // usable from cached LCD/PSRAM callers

void detect_mode_begin() {
    pinMode(detect_pin, INPUT_PULLDOWN);
    detect_mode = true;
}
void media_lost() {
    if (state.mounted()) ++losses;
    state.lost();
    // Do not destroy the volume or reinitialize the controller with live handles.
    detect_mode_begin();
    next_retry = millis() + retry_ms;
}
bool present() {
    if (!state.io_allowed() || detect_mode) return false;
    auto *card = SD.sdfs.card();
    // FIFO writes can return with a transfer pending. CMD13 clears IRQ flags,
    // so consume completion first, before probing (including between sectors).
    if (card && card->syncDevice() && card->status() != 0) return true;
    last_error = card ? card->errorCode() : 0xff;
    media_lost();
    return false;
}
class CardGate : public FsBlockDeviceInterface {
public:
    bool isBusy() override { return state.io_allowed() && SD.sdfs.card()->isBusy(); }
    uint32_t sectorCount() override { return state.io_allowed() ? SD.sdfs.card()->sectorCount() : 0; }
    bool syncDevice() override {
        if (!present()) return false;
        if (SD.sdfs.card()->syncDevice()) return true;
        failed(); return false;
    }
    bool readSector(uint32_t s, uint8_t *p) override { return readSectors(s,p,1); }
    bool writeSector(uint32_t s, const uint8_t *p) override { return writeSectors(s,p,1); }
    bool readSectors(uint32_t s, uint8_t *p, size_t n) override {
        for (size_t i=0; i<n; ++i) {
            if (!present()) return false;
            if (!SD.sdfs.card()->readSector(s+i,sector)) { failed(); return false; }
            memcpy(p+i*512,sector,512);
        }
        return true;
    }
    bool writeSectors(uint32_t s, const uint8_t *p, size_t n) override {
        for (size_t i=0; i<n; ++i) {
            if (!present()) return false;
            memcpy(sector,p+i*512,512);
            if (!SD.sdfs.card()->writeSector(s+i,sector)) { failed(); return false; }
        }
        return true;
    }
private:
    void failed() { last_error=SD.sdfs.card()->errorCode(); media_lost(); }
} gate;

bool mount_once() {
    if (!state.begin_mount()) return false;
    volume.end();
    SD.sdfs.end();
    const uint32_t start=millis();
    ++mount_attempts;
    detect_mode=false;
    // Only initialize the card here. All files use our guarded FsVolume.
    bool ok=SD.sdfs.cardBegin(SdioConfig(FIFO_SDIO));
    if (ok) ok=volume.begin(&gate,true);
    last_error=SD.sdfs.card() ? SD.sdfs.card()->errorCode() : 0xff;
    last_mount_ms=millis()-start;
    state.finish_mount(ok);
    if (!state.mounted()) {
        volume.end();
        detect_mode_begin();
    }
    next_retry=millis()+retry_ms;
    return state.mounted();
}
}

FsVolume *sk_sd_volume() { return &volume; }
bool sk_sd_media_ready() { return state.mounted() && present(); }
bool sk_sd_acquire() { return state.acquire(); }
void sk_sd_release() {
    const bool released = state.release();
    configASSERT(released);
    (void)released;
}
bool sk_sd_is_mounted() { StorageLock lock; return state.mounted(); }

esp_err_t sk_sd_mount() {
    StorageLock lock;
    if (sk_sd_media_ready()) return ESP_OK;
    if (state.handles()) return ESP_ERR_INVALID_STATE;
    state.allow_manual_mount();
    for (unsigned i=0; i<3; ++i) {
        if (mount_once()) return ESP_OK;
        if (i<2) vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_NOT_FOUND;
}
void sk_sd_poll() {
    StorageLock lock;
    const uint32_t now=millis();
    if (int32_t(now-next_probe)<0) return;
    next_probe=now+probe_ms;
    if (state.mounted()) { present(); return; }
    if (!detect_mode) detect_mode_begin();
    if (!digitalReadFast(detect_pin)) { state.removed(); return; }
    if (!state.ejected() && !state.handles() && int32_t(now-next_retry)>=0) mount_once();
}
void sk_sd_print_status() {
    StorageLock lock;
    sk_console_printf("SDIO: mounted=%s attempts=%lu elapsed=%lu ms error=0x%02x open=%u generation=%lu\r\n",
        state.mounted()?"yes":"no",(unsigned long)mount_attempts,
        (unsigned long)last_mount_ms,last_error,state.handles(),(unsigned long)state.generation());
}
// Bootstrap-only helpers are unused by the display profile. Keep references
// linkable without allowing untracked SD-library handles into the recovery path.
void sk_sd_list(const char *) { sk_console_print("Use the upstream ls command\r\n"); }
void sk_sd_cat(const char *) { sk_console_print("Use the upstream cat command\r\n"); }

extern "C" void solar_os_shell_cmd_sd(solar_os_context_t *ctx,int argc,char **argv) {
    StorageLock lock;
    auto *io=solar_os_context_shell_io(ctx);
    if (argc==2 && !strcmp(argv[1],"eject")) {
        if (state.handles()) {
            solar_os_shell_io_printf(io,"sd: busy (%u open handles); close files and apps first\n",state.handles());
            return;
        }
        if (state.mounted()) {
            FsFile root;
            bool ok=root.open(&volume,"/",O_RDONLY) && root.sync();
            ok=root.close() && ok;
            if (!ok) { solar_os_shell_io_writeln(io,"sd: sync failed; not ejected"); return; }
        }
        if (!state.eject()) {
            solar_os_shell_io_writeln(io,"sd: busy; not ejected");
            return;
        }
        volume.end();
        SD.sdfs.end();
        detect_mode_begin();
        solar_os_shell_io_writeln(io,"sd: ejected; safe to remove");
        return;
    }
    if (argc==2 && !strcmp(argv[1],"mount")) {
        if (state.handles() && !state.mounted()) {
            solar_os_shell_io_writeln(io,"sd: close stale file handles before remounting"); return;
        }
        sk_sd_mount();
    } else if (argc!=1 && !(argc==2 && !strcmp(argv[1],"status"))) {
        solar_os_shell_io_writeln(io,"usage: sd [status|mount|eject]"); return;
    }
    sk_sd_poll();
    solar_os_shell_io_printf(io,"SD: %s, %u open handles, generation=%lu, attempts=%lu, losses=%lu, error=0x%02x\n",
        state.mounted()?"/sd mounted":state.ejected()?"ejected":state.handles()?"unavailable (close stale file handles)":"not mounted",
        state.handles(),(unsigned long)state.generation(),(unsigned long)mount_attempts,
        (unsigned long)losses,last_error);
}
#endif
