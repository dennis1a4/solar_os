#if SK_USB_STORAGE
#include <arduino_freertos.h>
#include "usb_storage.h"
#include "usb_sector_batch.h"
#include "storage_lock.h"
#include <SD.h>
#include <errno.h>
#include "platform.h"
#include "sd_storage.h"
extern "C" {
#include "solar_os_shell_io.h"
#include "solar_os.h"
}

namespace {
volatile bool transport_failed;
// Eight sectors per USB command, staged outside the small internal heap.
DMAMEM uint8_t sector_buffer[4096] __attribute__((aligned(32)));
// One physical device and its first supported partition for the initial port.
class Drive : public USBDrive {
public:
    Drive() : USBDrive(static_cast<USBHost *>(nullptr)) {}
    bool readSector(uint32_t s, uint8_t *p) override { return readSectors(s, p, 1); }
    bool readSectors(uint32_t s, uint8_t *p, size_t n) override {
        return readBatches(s,n,[&](uint32_t,uint8_t *buf,size_t count) {
            memcpy(p,buf,count*512);p+=count*512;return true;
        });
    }
    bool readSectorsCallback(uint32_t s,uint8_t *p,size_t n,
        void (*callback)(uint32_t,uint8_t *,void *),void *context) override {
        if (!callback) return false;
        return readBatches(s,n,[&](uint32_t first,uint8_t *buf,size_t count) {
            for(size_t i=0;i<count;++i) {
                memcpy(p,buf+i*512,512);
                callback(first+i,p,context);
            }
            return true;
        });
    }
    template<class Deliver> bool readBatches(uint32_t s,size_t n,Deliver deliver) {
        if (transport_failed || msDriveInfo.capacity.BlockSize!=512) return false;
        return sk_usb_read_batches(s,n,sector_buffer,sizeof(sector_buffer)/512,
            [&](uint32_t first,uint8_t *buf,size_t count) {
                return !transport_failed && USBDrive::readSectors(first,buf,count) && !transport_failed;
            },deliver);
    }
    bool writeSector(uint32_t s, const uint8_t *p) override { return writeSectors(s, p, 1); }
    bool writeSectors(uint32_t s, const uint8_t *p, size_t n) override {
        if (transport_failed || msDriveInfo.capacity.BlockSize != 512) return false;
        for (size_t i=0; i<n; ++i) {
            memcpy(sector_buffer, p+i*512, 512);
            if (!USBDrive::writeSectors(s+i, sector_buffer, 1)) return false;
        }
        return true;
    }
protected:
    void disconnect() override {
        transport_failed = true;
        USBDrive::disconnect();
    }
} drive;
unsigned open_handles;
bool ejected;

// Keep stale file caches from ever writing to a newly inserted device.
// releasePartition runs in USB interrupt context and only clears this gate.
class Media : public FsBlockDeviceInterface {
public:
    volatile bool valid = false;
    bool isBusy() override { return valid && drive.isBusy(); }
    uint32_t sectorCount() override { return valid ? drive.sectorCount() : 0; }
    bool syncDevice() override { return valid && drive.syncDevice(); }
    bool readSector(uint32_t s, uint8_t *p) override {
        return valid && drive.readSector(s, p);
    }
    bool readSectors(uint32_t s, uint8_t *p, size_t n) override {
        return valid && drive.readSectors(s, p, n);
    }
    bool readSectorsCallback(uint32_t s,uint8_t *p,size_t n,
        void (*callback)(uint32_t,uint8_t *,void *),void *context) override {
        return valid && drive.readSectorsCallback(s,p,n,callback,context) && valid;
    }
    bool writeSector(uint32_t s, const uint8_t *p) override {
        return valid && drive.writeSector(s, p);
    }
    bool writeSectors(uint32_t s, const uint8_t *p, size_t n) override {
        return valid && drive.writeSectors(s, p, n);
    }
} media;

class Volume : public USBFilesystem {
    uint32_t first_sector=0,sector_count=0;
public:
    Volume() : USBFilesystem(static_cast<USBHost *>(nullptr)) {}
    bool claimPartition(USBDrive *d, int part, int kind, int type,
                        uint32_t first, uint32_t count, uint8_t *guid) override {
        if (ejected || open_handles || d->msDriveInfo.capacity.BlockSize != 512 ||
            !check_voltype_guid(kind, guid)) return false;
        media.valid = true;
        const bool mounted = mscfs.begin(&media, false, first, count);
        if (sk_sd_is_mounted()) {
#if SK_SD_RECOVERY
            sk_sd_volume()->chvol();
#else
            SD.sdfs.chvol();
#endif
        }
        if (!mounted) {
            media.valid = false;
            return false;
        }
        first_sector=first;sector_count=count;
        device = d;
        partition = part;
        partitionType = type;
        return true;
    }
    void releasePartition() override {
        media.valid = false;
        device = nullptr;
        // Do not destroy SdFat's volume while a task is using an open handle.
    }
    bool resetUsage() {
        // SdFat keeps FAT counts private. Reload its volume object to reset
        // accounting without replacing the USB device/partition registration.
        // No live file/directory handle may retain pointers to that object.
        if (!media.valid || open_handles) return false;
        FsFile root;
        bool ok=root.open(&mscfs,"/",O_RDONLY) && root.sync();
        ok=root.close() && ok;
        if (!ok) return false;
        mscfs.end();
        if (!mscfs.begin(&media,false,first_sector,sector_count)) {
            media.valid=false;return false;
        }
        return true;
    }
    void eject() {
        media.valid = false;
        mscfs.end();
        device = nullptr;
        mydevice = nullptr;
    }
} volume;
}

void sk_usb_storage_begin() {
    drive.whenToUpdateConnectedFilesystems(USBDrive::UPDATE_MANUAL);
}
bool sk_usb_storage_mounted() { return media.valid && bool(volume) && !ejected; }
bool sk_usb_storage_refresh_usage() {
    if (!sk_usb_storage_mounted()) { errno=ENODEV;return false; }
    if (open_handles) { errno=EBUSY;return false; }
    if (!volume.resetUsage()) { errno=EIO;return false; }
    return true;
}
FsVolume *sk_usb_storage_volume() { return &volume.mscfs; }
void sk_usb_storage_acquire() { ++open_handles; }
void sk_usb_storage_release() { configASSERT(open_handles); --open_handles; }
void sk_usb_storage_poll() {
    if (!drive) { ejected = false; transport_failed = false; }
    if (open_handles || ejected || transport_failed) return;
    if (!media.valid) volume.mscfs.end();
    drive.updateConnectedFilesystems();
}

extern "C" bool sk_usb_storage_transport_failed() { return transport_failed; }
extern "C" void sk_usb_storage_transport_fault() { transport_failed=true; media.valid=false; }

extern "C" void solar_os_shell_cmd_usb(solar_os_context_t *ctx, int argc, char **argv) {
    StorageLock lock;
    auto *io = solar_os_context_shell_io(ctx);
    if (argc == 2 && !strcmp(argv[1], "eject")) {
        if (open_handles) {
            solar_os_shell_io_printf(io, "usb: busy (%u open handles); close files and apps first\n", open_handles);
            return;
        }
        FsFile root;
        bool synced = true;
        if (sk_usb_storage_mounted()) {
            synced = root.open(&volume.mscfs, "/", O_RDONLY) && root.sync();
            synced = root.close() && synced;
        }
        if (!synced) {
            solar_os_shell_io_writeln(io, "usb: sync failed; not ejected");
            return;
        }
        ejected = true;
        volume.eject();
        solar_os_shell_io_writeln(io, "usb: ejected; safe to unplug");
        return;
    }
    if (argc == 2 && !strcmp(argv[1], "mount")) {
        if (open_handles && !sk_usb_storage_mounted()) {
            solar_os_shell_io_writeln(io, "usb: close stale file handles before remounting");
            return;
        }
        ejected = false;
        if (!sk_usb_storage_mounted() && drive && !transport_failed) {
            volume.eject();
            drive.startFilesystems();
        }
    } else if (argc != 1 && !(argc == 2 && !strcmp(argv[1], "status"))) {
        solar_os_shell_io_writeln(io, "usage: usb [status|mount|eject]");
        return;
    }
    if (sk_usb_storage_mounted()) {
        const unsigned type = volume.mscfs.fatType();
        char format[12];
        if (type == 64) strcpy(format, "exFAT");
        else snprintf(format, sizeof(format), "FAT%u", type);
        solar_os_shell_io_printf(io, "USB: /usb, %s, %llu MiB, %u open handles\n", format,
            (unsigned long long)(volume.totalSize() / (1024 * 1024)), open_handles);
    } else {
        solar_os_shell_io_printf(io, "USB: %s%s\n", drive ? "connected, not mounted" : "no drive",
            transport_failed ? " (transfer failed; unplug drive)" : ejected ? " (ejected)" : open_handles ? " (close stale file handles)" : "");
    }
}
#endif
