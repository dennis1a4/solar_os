#if SK_UPSTREAM_SHELL
#include <arduino_freertos.h>
#include <SD.h>
#include <new>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include "platform.h"
#include "solar_os_storage.h"

// Owned by the one shell task. Read-only during initial shell integration.
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
    out->st_mode = (file.isDirectory() ? S_IFDIR : S_IFREG) | 0444;
    out->st_size = file.size();
    out->st_nlink = 1;
    return 0;
}
static int file_read(void *cookie, char *data, int size) {
    int count = static_cast<File *>(cookie)->read(data, size);
    if (count < 0) errno = EIO;
    return count;
}
static fpos_t file_seek(void *cookie, fpos_t offset, int whence) {
    auto *file = static_cast<File *>(cookie);
    int64_t target = offset;
    if (whence == SEEK_CUR) target += file->position();
    else if (whence == SEEK_END) target += file->size();
    else if (whence != SEEK_SET) { errno = EINVAL; return -1; }
    if (target < 0 || target > INT32_MAX || !file->seek(uint32_t(target))) {
        errno = EINVAL; return -1;
    }
    return target;
}
static int file_close(void *cookie) { delete static_cast<File *>(cookie); return 0; }
extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    if (!storage_ready(path)) return nullptr;
    if (!mode || (strcmp(mode, "r") && strcmp(mode, "rb"))) {
        errno = EROFS; return nullptr;
    }
    auto *file = new (std::nothrow) File(SD.open(path, FILE_READ));
    if (!file) { errno = ENOMEM; return nullptr; }
    if (!*file || file->isDirectory()) {
        errno = *file ? EISDIR : ENOENT; delete file; return nullptr;
    }
    FILE *stream = funopen(file, file_read, nullptr, file_seek, file_close);
    if (!stream) delete file;
    return stream;
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
extern "C" esp_err_t solar_os_storage_mkdir(const char *) { return ESP_ERR_NOT_SUPPORTED; }
extern "C" esp_err_t solar_os_storage_sync_file(FILE *) { return ESP_ERR_NOT_SUPPORTED; }
#endif
