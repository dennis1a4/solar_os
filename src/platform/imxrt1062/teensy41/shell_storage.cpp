#if SK_UPSTREAM_SHELL
#include <arduino_freertos.h>
#include <SD.h>
#include <new>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include "platform.h"
#include "solar_os_storage.h"
#include "flash_storage.h"
#include "storage_lock.h"
#include "usb_storage.h"
#include "sd_storage.h"

// Serialize each storage operation across consoles and Playground's worker.
#include <semphr.h>
static StaticSemaphore_t storage_mutex_buffer;
static SemaphoreHandle_t storage_mutex;
StorageLock::StorageLock() {
    taskENTER_CRITICAL();
    if (!storage_mutex) storage_mutex=xSemaphoreCreateRecursiveMutexStatic(&storage_mutex_buffer);
    taskEXIT_CRITICAL();
    configASSERT(storage_mutex);
    xSemaphoreTakeRecursive(storage_mutex,portMAX_DELAY);
}
StorageLock::~StorageLock() { xSemaphoreGiveRecursive(storage_mutex); }
// libc streams use funopen; no syscall overrides affect FreeRTOS or USB.
struct Route {
    char path[SOLAR_OS_STORAGE_PATH_MAX];
    bool flash=false, root=false, mount_root=false, sd_alias=false, usb=false;
};
static bool route_path(const char *path, Route &r, bool require_ready=true) {
    if (!path || path[0]!='/') { errno=EINVAL; return false; }
    char normalized[SOLAR_OS_STORAGE_PATH_MAX];
    if (solar_os_storage_normalize_path(path,normalized,sizeof(normalized))!=ESP_OK) { errno=ENAMETOOLONG; return false; }
    const char *local=normalized;
#if SK_QSPI_FLASH
    if (!strncmp(local,"/flash",6) && (!local[6] || local[6]=='/')) { r.flash=true; local+=6; }
    else if (!strncmp(local,"/sd",3) && (!local[3] || local[3]=='/')) { r.sd_alias=true; local+=3; }
    r.root=!strcmp(normalized,"/");
#endif
#if SK_USB_STORAGE
    if (!r.flash && !r.sd_alias && !strncmp(local,"/usb",4) && (!local[4] || local[4]=='/')) { r.usb=true; local+=4; }
#endif
    strlcpy(r.path,*local ? local : "/",sizeof(r.path));
    r.mount_root=!strcmp(r.path,"/");
    if (!require_ready || r.root) return true;
#if SK_QSPI_FLASH
    if (r.flash) { if (sk_flash_mounted()) return true; errno=ENODEV; return false; }
#endif
#if SK_USB_STORAGE
    if (r.usb) { if (sk_usb_storage_mounted()) return true; errno=ENODEV; return false; }
#endif
#if SK_SD_RECOVERY
    if (!sk_sd_media_ready()) { errno=ENODEV; return false; }
#else
    if (!sk_sd_is_mounted()) { errno=ENODEV; return false; }
#endif
    return true;
}
static FsVolume *fat_volume(const Route &r) {
#if SK_USB_STORAGE
    if (r.usb) return sk_usb_storage_volume();
#endif
#if SK_SD_RECOVERY
    return sk_sd_volume();
#else
    return SD.sdfs.vol();
#endif
}
static bool media_ready(bool usb, bool sd) {
#if SK_USB_STORAGE
    if (usb && !sk_usb_storage_mounted()) { errno=ENODEV; return false; }
#endif
#if SK_SD_RECOVERY
    if (sd && !sk_sd_media_ready()) { errno=ENODEV; return false; }
#endif
    return true;
}
static bool media_acquire(bool usb, bool sd) {
#if SK_USB_STORAGE
    if (usb) sk_usb_storage_acquire();
#endif
#if SK_SD_RECOVERY
    if (sd && !sk_sd_acquire()) { errno=ENODEV; return false; }
#endif
    return true;
}
static void media_release(bool usb, bool sd) {
#if SK_USB_STORAGE
    if (usb) sk_usb_storage_release();
#endif
#if SK_SD_RECOVERY
    if (sd) sk_sd_release();
#endif
}
#if SK_QSPI_FLASH
static int flash_result(int value) {
    if (value>=0) return value;
    switch(value) {
    case LFS_ERR_NOENT: errno=ENOENT; break;
    case LFS_ERR_EXIST: errno=EEXIST; break;
    case LFS_ERR_NOTDIR: errno=ENOTDIR; break;
    case LFS_ERR_ISDIR: errno=EISDIR; break;
    case LFS_ERR_NOTEMPTY: errno=ENOTEMPTY; break;
    case LFS_ERR_BADF: errno=EBADF; break;
    case LFS_ERR_FBIG: errno=EFBIG; break;
    case LFS_ERR_INVAL: errno=EINVAL; break;
    case LFS_ERR_NOSPC: errno=ENOSPC; break;
    case LFS_ERR_NOMEM: errno=ENOMEM; break;
    case LFS_ERR_NAMETOOLONG: errno=ENAMETOOLONG; break;
    default: errno=EIO;
    }
    return -1;
}
#endif
struct sk_directory {
    FsFile file; struct dirent entry;
    bool flash=false, root=false, usb=false, sd=false; unsigned synthetic=0;
#if SK_QSPI_FLASH
    lfs_dir_t little{};
#endif
};
extern "C" DIR *opendir(const char *path) { StorageLock lock;
    Route r; if (!route_path(path,r)) return nullptr;
    auto *dir=new (std::nothrow) sk_directory;
    if (!dir) { errno=ENOMEM; return nullptr; }
    dir->flash=r.flash; dir->root=r.root; dir->usb=r.usb; dir->sd=!r.flash && !r.root && !r.usb;
#if SK_QSPI_FLASH
    if (r.flash) {
        if (flash_result(lfs_dir_open(sk_flash_fs(),&dir->little,r.path))<0) { delete dir; return nullptr; }
        sk_flash_acquire(); return dir;
    }
#endif
    // The namespace root lists mounts only. SD entries belong under /sd.
    if (r.root) return dir;
    dir->file=fat_volume(r)->open(r.path,O_RDONLY);
    if (!dir->file || !dir->file.isDirectory()) {
        errno=dir->file ? ENOTDIR : ENOENT; delete dir; return nullptr;
    }
    if (!media_acquire(dir->usb,dir->sd)) { delete dir; return nullptr; }
    return dir;
}
extern "C" struct dirent *readdir(DIR *dir) { StorageLock lock;
    if (!dir) { errno=EBADF; return nullptr; }
#if SK_QSPI_FLASH
    if (dir->flash) {
        lfs_info info; int n;
        do { n=flash_result(lfs_dir_read(sk_flash_fs(),&dir->little,&info)); }
        while(n>0 && (!strcmp(info.name,".") || !strcmp(info.name,"..")));
        if (n<=0) return nullptr;
        dir->entry.d_type=info.type==LFS_TYPE_DIR ? DT_DIR : DT_REG;
        strlcpy(dir->entry.d_name,info.name,sizeof(dir->entry.d_name)); return &dir->entry;
    }
    if (dir->root) while(dir->synthetic<3) {
        unsigned mount=dir->synthetic++;
        if ((mount==0 && sk_sd_is_mounted()) || (mount==1 && sk_flash_mounted())) {
            dir->entry.d_type=DT_DIR;
            strcpy(dir->entry.d_name,mount==0 ? "sd" : "flash"); return &dir->entry;
        }
#if SK_USB_STORAGE
        if (mount==2 && sk_usb_storage_mounted()) {
            dir->entry.d_type=DT_DIR;
            strcpy(dir->entry.d_name,"usb"); return &dir->entry;
        }
#endif
    }
#endif
    if (!media_ready(dir->usb,dir->sd) || !dir->file) return nullptr;
    for (;;) {
        FsFile entry=dir->file.openNextFile(O_RDONLY);
        if (!entry) return nullptr;
        dir->entry.d_type=entry.isDirectory() ? DT_DIR : DT_REG;
        entry.getName(dir->entry.d_name,sizeof(dir->entry.d_name)); return &dir->entry;
    }
}
extern "C" int closedir(DIR *dir) { StorageLock lock;
    if (!dir) { errno=EBADF; return -1; }
    int ret=0;
#if SK_QSPI_FLASH
    if (dir->flash) { ret=flash_result(lfs_dir_close(sk_flash_fs(),&dir->little)); sk_flash_release(); }
#endif
    bool usb=dir->usb, sd=dir->sd;
    if (!media_ready(usb,sd)) ret=-1;
    delete dir; media_release(usb,sd); return ret;
}
extern "C" int __wrap_stat(const char *path, struct stat *out) { StorageLock lock;
    if (!out) { errno=EINVAL; return -1; }
    Route r; if (!route_path(path,r)) return -1;
    memset(out,0,sizeof(*out)); out->st_nlink=1;
    if (r.root) { out->st_mode=S_IFDIR|0777; return 0; }
#if SK_QSPI_FLASH
    if (r.flash) {
        lfs_info info;
        if (flash_result(lfs_stat(sk_flash_fs(),r.path,&info))<0) return -1;
        out->st_mode=(info.type==LFS_TYPE_DIR ? S_IFDIR : S_IFREG)|0666;
        out->st_size=info.size; out->st_dev=1; return 0;
    }
#endif
    FsFile file=fat_volume(r)->open(r.path,O_RDONLY);
    if (!file) { errno=ENOENT; return -1; }
    out->st_mode=(file.isDirectory() ? S_IFDIR : S_IFREG)|0666;
    if (file.size()>INT32_MAX) { errno=EOVERFLOW; return -1; }
    out->st_size=file.size(); out->st_dev=r.usb ? 2 : 0; return 0;
}
static constexpr int first_fd=3, file_limit=16;
struct OpenFile {
    FsFile file; FILE *stream=nullptr; bool used=false, usb=false, sd=false, append=false; int access=O_RDONLY;
#if SK_QSPI_FLASH
    lfs_file_t *little=nullptr;
#endif
};
static OpenFile files[file_limit];
static OpenFile *lookup(int fd) {
    if (fd<first_fd || fd>=first_fd+file_limit || !files[fd-first_fd].used) { errno=EBADF; return nullptr; }
    return &files[fd-first_fd];
}
extern "C" int __wrap_open(const char *path,int flags,...) { StorageLock lock;
    Route r; if (!route_path(path,r)) return -1;
    if (flags & ~(O_ACCMODE|O_CREAT|O_TRUNC|O_APPEND|O_EXCL)) { errno=EINVAL; return -1; }
    if ((flags&O_ACCMODE)>O_RDWR || ((flags&O_TRUNC) && (flags&O_ACCMODE)==O_RDONLY)) { errno=EINVAL; return -1; }
    if (r.mount_root) { errno=EISDIR; return -1; }
    int slot=0;
    while(slot<file_limit && files[slot].used) ++slot;
    if (slot==file_limit) { errno=EMFILE; return -1; }
    auto &f=files[slot];
    f.usb=r.usb; f.sd=!r.flash && !r.usb; f.append=flags&O_APPEND;
#if SK_QSPI_FLASH
    if (r.flash) {
        int mode=(flags&O_ACCMODE)==O_RDONLY ? LFS_O_RDONLY : (flags&O_ACCMODE)==O_WRONLY ? LFS_O_WRONLY : LFS_O_RDWR;
        if (flags&O_CREAT) mode|=LFS_O_CREAT;
        if (flags&O_TRUNC) mode|=LFS_O_TRUNC;
        if (flags&O_APPEND) mode|=LFS_O_APPEND;
        if ((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL)) mode|=LFS_O_EXCL;
        f.little=new (std::nothrow) lfs_file_t{};
        if (!f.little) { errno=ENOMEM; return -1; }
        if (flash_result(lfs_file_open(sk_flash_fs(),f.little,r.path,mode))<0) { delete f.little; f.little=nullptr; return -1; }
        sk_flash_acquire();
    } else
#endif
    {
        if (fat_volume(r)->exists(r.path)) {
            FsFile check=fat_volume(r)->open(r.path,O_RDONLY);
            if (check && check.isDirectory()) { errno=EISDIR; return -1; }
            if ((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL)) { errno=EEXIST; return -1; }
        } else if (!(flags&O_CREAT)) { errno=ENOENT; return -1; }
        if (!f.file.open(fat_volume(r),r.path,flags)) { errno=EIO; return -1; }
    }
    if (!r.flash && f.file.fileSize()>INT32_MAX) {
        f.file.close(); errno=EFBIG; return -1;
    }
    if (!media_acquire(f.usb,f.sd)) { f.file.close(); return -1; }
    f.used=true; f.stream=nullptr; f.access=flags&O_ACCMODE; return first_fd+slot;
}
extern "C" ssize_t __wrap_read(int fd,void *data,size_t size) { StorageLock lock;
    auto *f=lookup(fd); if (!f || !media_ready(f->usb,f->sd)) return -1;
    if (f->access==O_WRONLY) { errno=EBADF; return -1; }
#if SK_QSPI_FLASH
    if (f->little) return flash_result(lfs_file_read(sk_flash_fs(),f->little,data,size));
#endif
    int n=f->file.read(data,size); if (n<0) errno=EIO; return n;
}
extern "C" ssize_t __wrap_write(int fd,const void *data,size_t size) { StorageLock lock;
    auto *f=lookup(fd); if (!f || !media_ready(f->usb,f->sd)) return -1;
    if (f->access==O_RDONLY) { errno=EBADF; return -1; }
#if SK_QSPI_FLASH
    if (f->little) return flash_result(lfs_file_write(sk_flash_fs(),f->little,data,size));
#endif
    if (size>size_t(INT32_MAX) || (f->append ? f->file.fileSize() : f->file.curPosition())+size>INT32_MAX) { errno=EFBIG; return -1; }
    size_t n=f->file.write(data,size);
    if (n<size) { errno=EIO; if (!n && size) return -1; } return n;
}
extern "C" int __wrap_fsync(int fd) { StorageLock lock;
    auto *f=lookup(fd); if (!f || !media_ready(f->usb,f->sd)) return -1;
#if SK_QSPI_FLASH
    if (f->little) return flash_result(lfs_file_sync(sk_flash_fs(),f->little));
#endif
    if (!f->file.sync()) { errno=EIO; return -1; } return 0;
}
extern "C" int __wrap_close(int fd) { StorageLock lock;
    auto *f=lookup(fd); if (!f) return -1;
    int ret=0;
#if SK_QSPI_FLASH
    if (f->little) {
        ret=flash_result(lfs_file_close(sk_flash_fs(),f->little));
        delete f->little; f->little=nullptr; sk_flash_release();
    } else
#endif
    if (!f->file.close()) { errno=EIO; ret=-1; }
    if (!media_ready(f->usb,f->sd)) ret=-1;
    media_release(f->usb,f->sd); f->usb=f->sd=false;
    f->used=false; f->stream=nullptr; return ret;
}
extern "C" off_t __wrap_lseek(int fd,off_t offset,int whence) { StorageLock lock;
    auto *f=lookup(fd); if (!f || !media_ready(f->usb,f->sd)) return -1;
#if SK_QSPI_FLASH
    if (f->little) {
        int mode=whence==SEEK_SET ? LFS_SEEK_SET : whence==SEEK_CUR ? LFS_SEEK_CUR : whence==SEEK_END ? LFS_SEEK_END : -1;
        if (mode<0) { errno=EINVAL; return -1; }
        return flash_result(lfs_file_seek(sk_flash_fs(),f->little,offset,mode));
    }
#endif
    int64_t target=offset;
    if (whence==SEEK_CUR) target+=f->file.curPosition();
    else if (whence==SEEK_END) target+=f->file.fileSize();
    else if (whence!=SEEK_SET) { errno=EINVAL; return -1; }
    if (target<0 || target>INT32_MAX || !f->file.seekSet(target)) { errno=EINVAL; return -1; } return target;
}
extern "C" int __wrap_fstat(int fd,struct stat *out) { StorageLock lock;
    auto *f=lookup(fd); if (!f || !media_ready(f->usb,f->sd)) return -1;
    if (!out) { errno=EINVAL; return -1; }
    memset(out,0,sizeof(*out)); out->st_mode=S_IFREG|0666; out->st_nlink=1;
#if SK_QSPI_FLASH
    if (f->little) {
        int n=flash_result(lfs_file_size(sk_flash_fs(),f->little)); if (n<0) return -1;
        out->st_size=n; out->st_dev=1; return 0;
    }
#endif
    out->st_size=f->file.fileSize(); out->st_dev=f->usb ? 2 : 0; return 0;
}
extern "C" int __real_fileno(FILE *stream);
extern "C" int __wrap_fileno(FILE *stream) { StorageLock lock;
    for (int i = 0; i < file_limit; ++i)
        if (files[i].used && files[i].stream == stream) return first_fd + i;
    return __real_fileno(stream);
}
static int file_read(void *cookie, char *data, int size) { return __wrap_read(int(intptr_t(cookie)), data, size); }
static int file_write(void *cookie, const char *data, int size) { return __wrap_write(int(intptr_t(cookie)), data, size); }
static fpos_t file_seek(void *cookie, fpos_t offset, int whence) { return __wrap_lseek(int(intptr_t(cookie)), offset, whence); }
static int file_close(void *cookie) { return __wrap_close(int(intptr_t(cookie))); }
extern "C" FILE *__wrap_fopen(const char *path, const char *mode) { StorageLock lock;
    if (!mode || !*mode) { errno = EINVAL; return nullptr; }
    bool plus = false, binary = false, exclusive = false;
    for (const char *m = mode + 1; *m; ++m) {
        if (*m == '+' && !plus) plus = true;
        else if (*m == 'b' && !binary) binary = true;
        else if (*m == 'x' && !exclusive && *mode == 'w') exclusive = true;
        else { errno = EINVAL; return nullptr; }
    }
    int flags;
    switch (*mode) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: errno = EINVAL; return nullptr;
    }
    if (exclusive) flags |= O_EXCL;
    int fd = __wrap_open(path, flags);
    if (fd < 0) return nullptr;
    FILE *stream = funopen(reinterpret_cast<void *>(intptr_t(fd)),
        (*mode == 'r' || plus) ? file_read : nullptr,
        (*mode != 'r' || plus) ? file_write : nullptr, file_seek, file_close);
    if (!stream) __wrap_close(fd);
    else files[fd-first_fd].stream = stream;
    return stream;
}
extern "C" int __wrap_mkdir(const char *path,mode_t) { StorageLock lock;
    Route r; if (!route_path(path,r)) return -1;
    if (r.mount_root) { errno=EEXIST; return -1; }
#if SK_QSPI_FLASH
    if (r.flash) return flash_result(lfs_mkdir(sk_flash_fs(),r.path));
#endif
    if (fat_volume(r)->exists(r.path)) { errno=EEXIST; return -1; }
    if (!fat_volume(r)->mkdir(r.path,false)) { errno=EIO; return -1; } return 0;
}
extern "C" int __wrap_unlink(const char *path) { StorageLock lock;
    struct stat info; if (__wrap_stat(path,&info)) return -1;
    if (S_ISDIR(info.st_mode)) { errno=EISDIR; return -1; }
    Route r; if (!route_path(path,r)) return -1;
#if SK_QSPI_FLASH
    if (r.flash) return flash_result(lfs_remove(sk_flash_fs(),r.path));
#endif
    if (!fat_volume(r)->remove(r.path)) { errno=EIO; return -1; } return 0;
}
extern "C" int __wrap_rmdir(const char *path) { StorageLock lock;
    Route r; if (!route_path(path,r)) return -1;
    if (r.mount_root) { errno=EBUSY; return -1; }
    struct stat info; if (__wrap_stat(path,&info)) return -1;
    if (!S_ISDIR(info.st_mode)) { errno=ENOTDIR; return -1; }
#if SK_QSPI_FLASH
    if (r.flash) return flash_result(lfs_remove(sk_flash_fs(),r.path));
#endif
    if (!fat_volume(r)->rmdir(r.path)) { errno=ENOTEMPTY; return -1; } return 0;
}
extern "C" int __wrap_remove(const char *path) { StorageLock lock;
    struct stat info; if (__wrap_stat(path,&info)) return -1;
    return S_ISDIR(info.st_mode) ? __wrap_rmdir(path) : __wrap_unlink(path);
}
extern "C" int __wrap_rename(const char *oldpath,const char *newpath) { StorageLock lock;
    Route a,b; if (!route_path(oldpath,a) || !route_path(newpath,b)) return -1;
    if (a.mount_root || b.mount_root) { errno=EBUSY; return -1; }
    if (a.flash!=b.flash || a.usb!=b.usb) { errno=EXDEV; return -1; }
    struct stat info; if (__wrap_stat(oldpath,&info)) return -1;
    if (!strcmp(a.path,b.path)) return 0;
    if (__wrap_stat(newpath,&info)==0) { errno=EEXIST; return -1; }
    if (errno!=ENOENT) return -1;
#if SK_QSPI_FLASH
    if (a.flash) return flash_result(lfs_rename(sk_flash_fs(),a.path,b.path));
#endif
    if (!fat_volume(a)->rename(a.path,b.path)) { errno=EIO; return -1; } return 0;
}
#if SK_SETTINGS
// LittleFS rename atomically replaces a regular destination. Keep general shell
// rename's no-clobber policy; only the settings transaction uses replacement.
extern "C" esp_err_t sk_settings_replace(const char *source, const char *dest) { StorageLock lock;
    constexpr const char *prefix="/flash/.solar-settings/";
    Route a,b;
    if (!route_path(source,a) || !route_path(dest,b) || !a.flash || !b.flash ||
        strncmp(source,prefix,strlen(prefix)) || strncmp(dest,prefix,strlen(prefix)) ||
        strncmp(a.path,"/.solar-settings/",17) || strncmp(b.path,"/.solar-settings/",17))
        return ESP_ERR_INVALID_ARG;
    return flash_result(lfs_rename(sk_flash_fs(),a.path,b.path))==0 ? ESP_OK : ESP_FAIL;
}
#endif
extern "C" bool solar_os_storage_flash_is_mounted() { StorageLock lock;
#if SK_QSPI_FLASH
    return sk_flash_mounted();
#else
    return false;
#endif
}
extern "C" bool solar_os_storage_is_mounted() { StorageLock lock; return solar_os_storage_mount_count()!=0; }
extern "C" esp_err_t solar_os_storage_get_usage_for_path(const char *path,solar_os_storage_usage_t *out) {
    StorageLock lock;
    if(!out)return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));
    Route r;
    if(!route_path(path,r))return ESP_ERR_INVALID_STATE;
    if(r.root)return ESP_ERR_INVALID_ARG;
#if SK_QSPI_FLASH
    if(r.flash) {
        auto *fs=sk_flash_fs();
        const lfs_ssize_t used=lfs_fs_size(fs);
        if(used<0)return ESP_FAIL;
        out->total_bytes=uint64_t(fs->cfg->block_count)*fs->cfg->block_size;
        out->used_bytes=uint64_t(used)*fs->cfg->block_size;
    } else
#endif
    {
        auto *volume=fat_volume(r);
        if(!volume)return ESP_ERR_INVALID_STATE;
        const int32_t free_clusters=volume->freeClusterCount();
        if(free_clusters<0 || !media_ready(r.usb,!r.usb))return ESP_FAIL;
        const uint64_t cluster_bytes=uint64_t(volume->sectorsPerCluster())*512;
        out->total_bytes=uint64_t(volume->clusterCount())*cluster_bytes;
        out->free_bytes=uint64_t(free_clusters)*cluster_bytes;
        if(out->free_bytes>out->total_bytes)return ESP_FAIL;
        out->used_bytes=out->total_bytes-out->free_bytes;
    }
    if(out->used_bytes>out->total_bytes)return ESP_FAIL;
    out->free_bytes=out->total_bytes-out->used_bytes;
    return ESP_OK;
}
extern "C" bool solar_os_storage_sd_is_mounted() { StorageLock lock; return sk_sd_is_mounted(); }
extern "C" bool solar_os_storage_root_is_mounted() { StorageLock lock;
#if SK_QSPI_FLASH
    return false; // Root is the mount list; no filesystem owns it.
#else
    return sk_sd_is_mounted();
#endif
}
extern "C" size_t solar_os_storage_mount_count() { StorageLock lock;
    return size_t(sk_sd_is_mounted()) + size_t(solar_os_storage_flash_is_mounted())
#if SK_USB_STORAGE
        + size_t(sk_usb_storage_mounted())
#endif
        ;
}
extern "C" bool solar_os_storage_get_mount(size_t index, solar_os_storage_mount_info_t *out) { StorageLock lock;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (sk_sd_is_mounted()) {
        if (!index) {
            strlcpy(out->mount_point, solar_os_storage_sd_mount_point(), sizeof(out->mount_point));
            strlcpy(out->name, "SD", sizeof(out->name));
            out->type = SOLAR_OS_STORAGE_MOUNT_SD;
            return true;
        }
        --index;
    }
    if (solar_os_storage_flash_is_mounted()) {
      if (!index) {
        strlcpy(out->mount_point, "/flash", sizeof(out->mount_point));
        strlcpy(out->name, "QSPI flash", sizeof(out->name));
        out->type = SOLAR_OS_STORAGE_MOUNT_FLASH;
        return true;
      }
      --index;
    }
#if SK_USB_STORAGE
    if (!index && sk_usb_storage_mounted()) {
        strlcpy(out->mount_point,"/usb",sizeof(out->mount_point));
        strlcpy(out->name,"USB",sizeof(out->name));
        out->type=SOLAR_OS_STORAGE_MOUNT_USB;
        return true;
    }
#endif
    return false;
}
extern "C" const char *solar_os_storage_mount_point() { StorageLock lock; return "/"; }
extern "C" const char *solar_os_storage_sd_mount_point() { StorageLock lock;
#if SK_QSPI_FLASH
    return "/sd";
#else
    return "/";
#endif
}
extern "C" const char *solar_os_storage_flash_mount_point() { StorageLock lock; return "/flash"; }
extern "C" bool solar_os_storage_path_has_mount_prefix(const char *path) { StorageLock lock; return path && path[0]=='/'; }
extern "C" esp_err_t solar_os_storage_path_mount_point(const char *path,char *out,size_t len) { StorageLock lock;
    if (!out) return ESP_ERR_INVALID_ARG;
    Route r; if (!route_path(path,r,false)) return ESP_ERR_INVALID_ARG;
    const char *mount=r.flash ? "/flash" : "/";
#if SK_QSPI_FLASH
    if (r.sd_alias) mount="/sd";
#endif
    if (r.usb) mount="/usb";
    if (strlen(mount)>=len) return ESP_ERR_INVALID_SIZE;
    strcpy(out,mount); return ESP_OK;
}
extern "C" esp_err_t solar_os_storage_mkdir(const char *p) { StorageLock lock; return __wrap_mkdir(p,0777)==0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_rmdir(const char *p) { StorageLock lock; return __wrap_rmdir(p)==0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_remove(const char *p) { StorageLock lock; return __wrap_remove(p)==0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_rename(const char *a,const char *b) { StorageLock lock;
    if (__wrap_rename(a,b)==0) return ESP_OK;
    if (errno!=EXDEV) return ESP_FAIL;
    // POSIX rename remains EXDEV; the OS move service can copy a regular file
    // between volumes, closing/syncing the destination before deleting source.
    struct stat info; if (__wrap_stat(a,&info)) return ESP_FAIL;
    if (!S_ISREG(info.st_mode)) { errno=EXDEV; return ESP_ERR_NOT_SUPPORTED; }
    esp_err_t err=solar_os_storage_copy_file(a,b);
    if (err!=ESP_OK) return err;
    return __wrap_unlink(a)==0 ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t solar_os_storage_sync_file(FILE *f) { StorageLock lock;
    if (!f) return ESP_ERR_INVALID_ARG;
    return fflush(f)==0 && __wrap_fsync(__wrap_fileno(f))==0 ? ESP_OK : ESP_FAIL;
}
#endif
