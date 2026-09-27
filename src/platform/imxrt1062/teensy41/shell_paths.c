#if SK_UPSTREAM_SHELL
#include "solar_os_storage.h"
#include <string.h>
static const char *storage_default_base_path(void) { return solar_os_storage_mount_point(); }
// Same path semantics as the upstream storage service.
static esp_err_t storage_append_path_segment(char *out,
                                             size_t out_len,
                                             const char *segment,
                                             size_t segment_len)
{
    const size_t out_used = strlen(out);
    const bool needs_slash = !(out_used == 1 && out[0] == '/');
    const size_t slash_len = needs_slash ? 1 : 0;

    if (out_used + slash_len + segment_len >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (needs_slash) {
        out[out_used] = '/';
    }
    memcpy(&out[out_used + slash_len], segment, segment_len);
    out[out_used + slash_len + segment_len] = '\0';
    return ESP_OK;
}

static void storage_pop_path_segment(char *out, size_t root_len)
{
    const size_t len = strlen(out);
    if (len <= root_len) {
        out[root_len] = '\0';
        return;
    }

    char *slash = strrchr(out, '/');
    if (slash == NULL || (size_t)(slash - out) <= root_len) {
        out[root_len] = '\0';
        return;
    }

    *slash = '\0';
}

esp_err_t solar_os_storage_join_path(const char *base_path,
                                     const char *relative_path,
                                     char *path,
                                     size_t path_len)
{
    if (base_path == NULL || base_path[0] != '/' || path == NULL || path_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t base_len = strlen(base_path);
    while (base_len > 1 && base_path[base_len - 1] == '/') {
        base_len--;
    }
    if (base_len >= path_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(path, base_path, base_len);
    path[base_len] = '\0';
    const size_t root_len = base_len;

    const char *cursor = relative_path;
    if (cursor == NULL) {
        return ESP_OK;
    }

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        const char *segment = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        const size_t segment_len = (size_t)(cursor - segment);

        if (segment_len == 0 ||
            (segment_len == 1 && segment[0] == '.')) {
            continue;
        }
        if (segment_len == 2 && segment[0] == '.' && segment[1] == '.') {
            storage_pop_path_segment(path, root_len);
            continue;
        }

        esp_err_t ret = storage_append_path_segment(path, path_len, segment, segment_len);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t solar_os_storage_default_path(const char *relative_path, char *path, size_t path_len)
{
    return solar_os_storage_join_path(solar_os_storage_mount_point(),
                                      relative_path,
                                      path,
                                      path_len);
}

esp_err_t solar_os_storage_normalize_path(const char *path, char *out, size_t out_len)
{
    if (path == NULL || path[0] == '\0' || out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // One namespace: /sd and /flash are mount points, and .. can leave either.
    if (path[0] != '/') return ESP_ERR_INVALID_ARG;
    const char root[] = "/";
    esp_err_t ret = ESP_OK;

    const size_t root_len = strlen(root);
    if (root_len >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    strlcpy(out, root, out_len);

    const char *cursor = path + root_len;
    while (*cursor == '/') {
        cursor++;
    }

    while (*cursor != '\0') {
        const char *segment = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        const size_t segment_len = (size_t)(cursor - segment);

        while (*cursor == '/') {
            cursor++;
        }

        if (segment_len == 0 ||
            (segment_len == 1 && segment[0] == '.')) {
            continue;
        }

        if (segment_len == 2 && segment[0] == '.' && segment[1] == '.') {
            storage_pop_path_segment(out, root_len);
            continue;
        }

        ret = storage_append_path_segment(out, out_len, segment, segment_len);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t solar_os_storage_resolve_path_at(const char *cwd,
                                           const char *arg,
                                           char *path,
                                           size_t path_len)
{
    if (path == NULL || path_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *base = cwd;
    if (!solar_os_storage_path_has_mount_prefix(base)) {
        base = storage_default_base_path();
    }

    char raw[SOLAR_OS_STORAGE_PATH_MAX];
    int written = 0;
    if (arg == NULL || arg[0] == '\0') {
        if (strlcpy(raw, base, sizeof(raw)) >= sizeof(raw)) {
            return ESP_ERR_INVALID_SIZE;
        }
    } else if (arg[0] == '/') {
        if (solar_os_storage_path_has_mount_prefix(arg)) {
            if (strlcpy(raw, arg, sizeof(raw)) >= sizeof(raw)) {
                return ESP_ERR_INVALID_SIZE;
            }
        } else {
            const char *default_base = storage_default_base_path();
            written = strcmp(default_base, "/") == 0 ?
                snprintf(raw, sizeof(raw), "%s", arg) :
                snprintf(raw, sizeof(raw), "%s%s", default_base, arg);
            if (written < 0 || (size_t)written >= sizeof(raw)) {
                return ESP_ERR_INVALID_SIZE;
            }
        }
    } else {
        written = strcmp(base, "/") == 0 ?
            snprintf(raw, sizeof(raw), "/%s", arg) :
            snprintf(raw, sizeof(raw), "%s/%s", base, arg);
        if (written < 0 || (size_t)written >= sizeof(raw)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    return solar_os_storage_normalize_path(raw, path, path_len);
}

esp_err_t solar_os_storage_resolve_path(const char *arg, char *path, size_t path_len)
{
    return solar_os_storage_resolve_path_at(NULL, arg, path, path_len);
}


#endif
