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

// Owned by the one shell task, including the synchronous Python VM.
// libc streams use funopen; no syscall overrides affect FreeRTOS or USB.
struct sk_directory { File file; struct dirent entry; };
static bool storage_ready(const char *path) {
    if (!path || path[0] != '/') { errno = EINVAL; return false; }
    if (!sk_sd_is_mounted()) { errno = ENODEV; return false; }
    return true;
}
extern "C" DIR *opendir(const char *path) {
    if (!storage_ready(path)) return nullptr;
    auto *dir = new (std::nothrow) sk_directory;
    if (!dir) { errno = ENOMEM; return nullptr; }
    dir->file = SD.open(path, FILE_READ);
    if (!dir->file || !dir->file.isDirectory()) {
        errno = dir->file ? ENOTDIR : ENOENT;
        delete dir; return nullptr;
    }
    return dir;
}
extern "C" struct dirent *readdir(DIR *dir) {
    if (!dir) { errno = EBADF; return nullptr; }
    File entry = dir->file.openNextFile(FILE_READ);
    if (!entry) return nullptr;
    dir->entry.d_type = entry.isDirectory() ? DT_DIR : DT_REG;
    strlcpy(dir->entry.d_name, entry.name(), sizeof(dir->entry.d_name));
    return &dir->entry;
}
extern "C" int closedir(DIR *dir) {
    if (!dir) { errno = EBADF; return -1; }
    delete dir; return 0;
}
extern "C" int __wrap_stat(const char *path, struct stat *out) {
    if (!out) { errno = EINVAL; return -1; }
    if (!storage_ready(path)) return -1;
    File file = SD.open(path, FILE_READ);
    if (!file) { errno = ENOENT; return -1; }
    memset(out, 0, sizeof(*out));
    out->st_mode = (file.isDirectory() ? S_IFDIR : S_IFREG) | 0666;
    out->st_size = file.size();
    out->st_nlink = 1;
    return 0;
}
// Descriptor slots are shared by stdio streams and MicroPython's POSIX reader.
static constexpr int first_fd = 3, file_limit = 16;
struct OpenFile { FsFile file; FILE *stream = nullptr; bool used = false; };
static OpenFile files[file_limit];
static OpenFile *lookup(int fd) {
    if (fd < first_fd || fd >= first_fd + file_limit || !files[fd-first_fd].used) {
        errno = EBADF; return nullptr;
    }
    return &files[fd-first_fd];
}
extern "C" int __wrap_open(const char *path, int flags, ...) {
    if (!storage_ready(path)) return -1;
    if (flags & ~(O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND | O_EXCL)) { errno = EINVAL; return -1; }
    // Do not truncate until a descriptor is available.
    int slot = 0;
    while (slot < file_limit && files[slot].used) ++slot;
    if (slot == file_limit) { errno = EMFILE; return -1; }
    if (SD.exists(path)) {
        File check = SD.open(path, FILE_READ);
        if (check && check.isDirectory()) { errno = EISDIR; return -1; }
        if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) { errno = EEXIST; return -1; }
    } else if (!(flags & O_CREAT)) { errno = ENOENT; return -1; }
    if (!files[slot].file.open(path, flags)) { errno = EIO; return -1; }
    files[slot].used = true;
    files[slot].stream = nullptr;
    return slot + first_fd;
}
extern "C" ssize_t __wrap_read(int fd, void *data, size_t size) {
    auto *f = lookup(fd); if (!f) return -1;
    int count = f->file.read(data, size);
    if (count < 0) errno = EIO;
    return count;
}
extern "C" ssize_t __wrap_write(int fd, const void *data, size_t size) {
    auto *f = lookup(fd); if (!f) return -1;
    size_t count = f->file.write(data, size);
    if (count < size) { errno = EIO; if (!count && size) return -1; }
    return count;
}
extern "C" int __wrap_fsync(int fd) {
    auto *f = lookup(fd); if (!f) return -1;
    if (!f->file.sync()) { errno = EIO; return -1; }
    return 0;
}
extern "C" int __wrap_close(int fd) {
    auto *f = lookup(fd); if (!f) return -1;
    bool ok = f->file.close();
    f->used = false; f->stream = nullptr;
    if (!ok) { errno = EIO; return -1; }
    return 0;
}
extern "C" off_t __wrap_lseek(int fd, off_t offset, int whence) {
    auto *f = lookup(fd); if (!f) return -1;
    int64_t target = offset;
    if (whence == SEEK_CUR) target += f->file.curPosition();
    else if (whence == SEEK_END) target += f->file.fileSize();
    else if (whence != SEEK_SET) { errno = EINVAL; return -1; }
    if (target < 0 || target > INT32_MAX || !f->file.seekSet(target)) { errno = EINVAL; return -1; }
    return target;
}
extern "C" int __wrap_fstat(int fd, struct stat *out) {
    auto *f = lookup(fd); if (!f) return -1;
    if (!out) { errno = EINVAL; return -1; }
    memset(out, 0, sizeof(*out)); out->st_mode = S_IFREG | 0666;
    out->st_size = f->file.fileSize(); out->st_nlink = 1;
    return 0;
}
extern "C" int __real_fileno(FILE *stream);
extern "C" int __wrap_fileno(FILE *stream) {
    for (int i = 0; i < file_limit; ++i)
        if (files[i].used && files[i].stream == stream) return first_fd + i;
    return __real_fileno(stream);
}
static int file_read(void *cookie, char *data, int size) { return __wrap_read(int(intptr_t(cookie)), data, size); }
static int file_write(void *cookie, const char *data, int size) { return __wrap_write(int(intptr_t(cookie)), data, size); }
static fpos_t file_seek(void *cookie, fpos_t offset, int whence) { return __wrap_lseek(int(intptr_t(cookie)), offset, whence); }
static int file_close(void *cookie) { return __wrap_close(int(intptr_t(cookie))); }
extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    if (!mode || !*mode) { errno = EINVAL; return nullptr; }
    bool plus = false, binary = false;
    for (const char *m = mode + 1; *m; ++m) {
        if (*m == '+' && !plus) plus = true;
        else if (*m == 'b' && !binary) binary = true;
        else { errno = EINVAL; return nullptr; }
    }
    int flags;
    switch (*mode) {
    case 'r': flags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: errno = EINVAL; return nullptr;
    }
    int fd = __wrap_open(path, flags);
    if (fd < 0) return nullptr;
    FILE *stream = funopen(reinterpret_cast<void *>(intptr_t(fd)),
        (*mode == 'r' || plus) ? file_read : nullptr,
        (*mode != 'r' || plus) ? file_write : nullptr, file_seek, file_close);
    if (!stream) __wrap_close(fd);
    else files[fd-first_fd].stream = stream;
    return stream;
}
extern "C" int __wrap_mkdir(const char *path, mode_t) {
    if (!storage_ready(path)) return -1;
    if (SD.exists(path)) { errno = EEXIST; return -1; }
    if (!SD.sdfs.mkdir(path, false)) { errno = EIO; return -1; }
    return 0;
}
extern "C" int __wrap_unlink(const char *path) {
    struct stat info;
    if (__wrap_stat(path, &info)) return -1;
    if (S_ISDIR(info.st_mode)) { errno = EISDIR; return -1; }
    if (!SD.sdfs.remove(path)) { errno = EIO; return -1; }
    return 0;
}
extern "C" int __wrap_rmdir(const char *path) {
    struct stat info;
    if (__wrap_stat(path, &info)) return -1;
    if (!S_ISDIR(info.st_mode)) { errno = ENOTDIR; return -1; }
    if (!strcmp(path, "/")) { errno = EBUSY; return -1; }
    if (!SD.sdfs.rmdir(path)) { errno = ENOTEMPTY; return -1; }
    return 0;
}
extern "C" int __wrap_remove(const char *path) {
    struct stat info;
    if (__wrap_stat(path, &info)) return -1;
    return S_ISDIR(info.st_mode) ? __wrap_rmdir(path) : __wrap_unlink(path);
}
extern "C" int __wrap_rename(const char *oldpath, const char *newpath) {
    if (!storage_ready(oldpath) || !storage_ready(newpath)) return -1;
    if (!SD.exists(oldpath)) { errno = ENOENT; return -1; }
    if (!strcmp(oldpath, newpath)) return 0;
    // SdFat cannot atomically replace an existing name; require an unused name.
    if (SD.exists(newpath)) { errno = EEXIST; return -1; }
    if (!SD.sdfs.rename(oldpath, newpath)) { errno = EIO; return -1; }
    return 0;
}
extern "C" bool solar_os_storage_is_mounted() { return sk_sd_is_mounted(); }
extern "C" bool solar_os_storage_sd_is_mounted() { return sk_sd_is_mounted(); }
extern "C" bool solar_os_storage_flash_is_mounted() { return false; }
extern "C" bool solar_os_storage_root_is_mounted() { return sk_sd_is_mounted(); }
extern "C" const char *solar_os_storage_mount_point() { return "/"; }
extern "C" const char *solar_os_storage_sd_mount_point() { return "/"; }
extern "C" const char *solar_os_storage_flash_mount_point() { return "/flash"; }
extern "C" bool solar_os_storage_path_has_mount_prefix(const char *path) { return path && path[0] == '/'; }
extern "C" esp_err_t solar_os_storage_path_mount_point(const char *path, char *out, size_t len) {
    if (!solar_os_storage_path_has_mount_prefix(path) || !out) return ESP_ERR_INVALID_ARG;
    if (len < 2) return ESP_ERR_INVALID_SIZE;
    strcpy(out, "/"); return ESP_OK;
}
extern "C" esp_err_t solar_os_storage_mkdir(const char *p) { return __wrap_mkdir(p, 0777) == 0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_rmdir(const char *p) { return __wrap_rmdir(p) == 0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_remove(const char *p) { return __wrap_remove(p) == 0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_rename(const char *a, const char *b) { return __wrap_rename(a,b) == 0 ? ESP_OK : ESP_FAIL; }
extern "C" esp_err_t solar_os_storage_sync_file(FILE *f) {
    if (!f) return ESP_ERR_INVALID_ARG;
    return fflush(f) == 0 && __wrap_fsync(__wrap_fileno(f)) == 0 ? ESP_OK : ESP_FAIL;
}
#endif
