#if SK_UPSTREAM_SHELL
#include "solar_os_storage.h"
#include "solar_os_log.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define SOLAR_OS_STORAGE_COPY_BUFFER_SIZE 512
#define ESP_LOGW SOLAR_OS_LOGW
static const char *TAG = "storage";
esp_err_t solar_os_storage_sibling_path(const char *path,
                                        const char *suffix,
                                        char *out,
                                        size_t out_len)
{
    if (path == NULL || path[0] == '\0' || suffix == NULL || suffix[0] == '\0' ||
        out == NULL || out_len == 0U) {
        errno = EINVAL;
        return ESP_ERR_INVALID_ARG;
    }
    const int written = snprintf(out, out_len, "%s%s", path, suffix);
    return written >= 0 && (size_t)written < out_len ?
        ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t solar_os_storage_replace_file(const char *staged_path,
                                        const char *active_path,
                                        const char *backup_path)
{
    if (staged_path == NULL || staged_path[0] == '\0' ||
        active_path == NULL || active_path[0] == '\0' ||
        backup_path == NULL || backup_path[0] == '\0' ||
        strcmp(staged_path, active_path) == 0 ||
        strcmp(staged_path, backup_path) == 0 ||
        strcmp(active_path, backup_path) == 0) {
        errno = EINVAL;
        return ESP_ERR_INVALID_ARG;
    }

    struct stat info;
    if (stat(staged_path, &info) != 0 || !S_ISREG(info.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }

    const bool had_active = stat(active_path, &info) == 0;
    if (stat(backup_path, &info) == 0 && remove(backup_path) != 0) {
        return ESP_FAIL;
    }
    if (had_active && rename(active_path, backup_path) != 0) {
        return ESP_FAIL;
    }

    if (rename(staged_path, active_path) != 0) {
        const int replace_errno = errno;
        if (had_active && rename(backup_path, active_path) != 0) {
            ESP_LOGW(TAG, "file replacement rollback failed: %s", active_path);
        }
        errno = replace_errno;
        return ESP_FAIL;
    }

    if (had_active && remove(backup_path) != 0) {
        ESP_LOGW(TAG, "stale replacement backup remains: %s", backup_path);
    }
    return ESP_OK;
}

esp_err_t solar_os_storage_read_file(const char *path,
                                     void *buffer,
                                     size_t buffer_len,
                                     size_t *read_len)
{
    if (path == NULL || path[0] == '\0' || buffer == NULL || buffer_len == 0) {
        errno = EINVAL;
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    const size_t length = fread(buffer, 1, buffer_len, file);
    const bool read_failed = ferror(file) != 0;
    const int read_errno = read_failed ? errno : 0;
    const int close_ret = fclose(file);
    const int close_errno = close_ret != 0 ? errno : 0;
    if (read_failed || close_ret != 0) {
        errno = read_errno != 0 ? read_errno : (close_errno != 0 ? close_errno : EIO);
        return ESP_FAIL;
    }

    if (read_len != NULL) {
        *read_len = length;
    }
    return ESP_OK;
}

esp_err_t solar_os_storage_copy_file_progress_cancel(
    const char *source_path,
    const char *dest_path,
    solar_os_storage_copy_progress_fn progress,
    solar_os_storage_cancel_fn should_cancel,
    void *user)
{
    if (source_path == NULL || source_path[0] == '\0' || dest_path == NULL || dest_path[0] == '\0') {
        errno = EINVAL;
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(source_path, dest_path) == 0) {
        errno = EINVAL;
        return ESP_ERR_INVALID_ARG;
    }

    FILE *source = fopen(source_path, "rb");
    if (source == NULL) {
        return ESP_FAIL;
    }

    struct stat source_st;
    if (fstat(fileno(source), &source_st) != 0 || source_st.st_size < 0) {
        const int stat_errno = errno;
        fclose(source);
        errno = stat_errno;
        return ESP_FAIL;
    }
    const uint64_t bytes_total = (uint64_t)source_st.st_size;

    // This initial SdFat profile refuses replacement. Besides preserving the
    // destination, this protects against FAT case/short-name aliases of source.
    struct stat dest_st;
    if (stat(dest_path, &dest_st) == 0 || errno != ENOENT) {
        fclose(source);
        errno = EEXIST;
        return ESP_FAIL;
    }
    FILE *dest = fopen(dest_path, "wb");
    if (dest == NULL) {
        const int open_errno = errno;
        fclose(source);
        errno = open_errno;
        return ESP_FAIL;
    }

    uint8_t buffer[SOLAR_OS_STORAGE_COPY_BUFFER_SIZE];
    esp_err_t ret = ESP_OK;
    uint64_t bytes_done = 0U;

    if (progress != NULL) {
        progress(0U, bytes_total, user);
    }

    while (true) {
        if (should_cancel != NULL && should_cancel(user)) {
            errno = ECANCELED;
            ret = ESP_ERR_INVALID_STATE;
            break;
        }
        const size_t bytes_read = fread(buffer, 1, sizeof(buffer), source);
        if (bytes_read > 0 && fwrite(buffer, 1, bytes_read, dest) != bytes_read) {
            ret = ESP_FAIL;
            break;
        }
        bytes_done += bytes_read;
        if (bytes_read > 0U && progress != NULL) {
            progress(bytes_done, bytes_total, user);
        }

        if (bytes_read < sizeof(buffer)) {
            if (ferror(source)) {
                ret = ESP_FAIL;
            }
            break;
        }
    }

    const int copy_errno = errno;
    if (fclose(dest) != 0 && ret == ESP_OK) {
        ret = ESP_FAIL;
    }
    const int close_errno = errno;
    fclose(source);

    if (ret != ESP_OK) {
        const int failure_errno = close_errno != 0 ?
            close_errno : (copy_errno != 0 ? copy_errno : EIO);
        (void)remove(dest_path);
        errno = failure_errno;
    }
    return ret;
}

esp_err_t solar_os_storage_copy_file_progress(
    const char *source_path,
    const char *dest_path,
    solar_os_storage_copy_progress_fn progress,
    void *user)
{
    return solar_os_storage_copy_file_progress_cancel(source_path,
                                                      dest_path,
                                                      progress,
                                                      NULL,
                                                      user);
}

esp_err_t solar_os_storage_copy_file(const char *source_path,
                                     const char *dest_path)
{
    return solar_os_storage_copy_file_progress(source_path,
                                               dest_path,
                                               NULL,
                                               NULL);
}

#endif
