#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "solar_os_storage.h"
const char *solar_os_storage_mount_point(void) { return "/"; }
bool solar_os_storage_path_has_mount_prefix(const char *p) { return p && p[0] == '/'; }
esp_err_t solar_os_storage_path_mount_point(const char *p, char *out, size_t len) {
    if (!p || p[0] != '/' || !out) return ESP_ERR_INVALID_ARG;
    if (len < 2) return ESP_ERR_INVALID_SIZE;
    strcpy(out, "/"); return ESP_OK;
}
int main(void) {
    char path[160];
    assert(solar_os_storage_resolve_path_at("/one/two", "../test.txt", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/one/test.txt") == 0);
    assert(solar_os_storage_resolve_path_at("/one", "/two/../test.txt", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/test.txt") == 0);
    assert(solar_os_storage_resolve_path_at("/", "../../../test.txt", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/test.txt") == 0);
    assert(solar_os_storage_join_path("/.shell", "../../history", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/.shell/history") == 0);
    assert(solar_os_storage_resolve_path_at("/", "too-long", path, 4) == ESP_ERR_INVALID_SIZE);
    assert(solar_os_storage_normalize_path(NULL, path, sizeof(path)) == ESP_ERR_INVALID_ARG);
    assert(solar_os_storage_resolve_path_at("/one", NULL, path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/one") == 0);
    assert(solar_os_storage_resolve_path_at("/flash", "..", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/") == 0);
    assert(solar_os_storage_normalize_path("/flash/../sd/data.bin", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/sd/data.bin") == 0);
    assert(solar_os_storage_normalize_path("//sd///flash/./data.bin", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/sd/flash/data.bin") == 0);
    assert(solar_os_storage_normalize_path("/flashish/data.bin", path, sizeof(path)) == ESP_OK);
    assert(strcmp(path, "/flashish/data.bin") == 0);
    puts("Teensy storage paths: cwd, absolute paths, traversal bounds and length errors passed");
}
