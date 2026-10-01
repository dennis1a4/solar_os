#include "solar_os_completion.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

bool solar_os_completion_files(const solar_os_completion_request_t *r,
                               solar_os_completion_emit_t emit, void *sink, void *user)
{
    solar_os_completion_files_t *fs = user;
    if (!fs || !fs->cwd || fs->cwd[0] != '/') return false;
    char directory[512], candidate[256], full[768];
    const char *slash = strrchr(r->prefix, '/');
    size_t base = slash ? (size_t)(slash-r->prefix)+1 : 0;
    int written = r->prefix[0] == '/' ? snprintf(directory, sizeof(directory), "%.*s", (int)base, r->prefix)
        : snprintf(directory, sizeof(directory), "%s/%.*s", fs->cwd, (int)base, r->prefix);
    if (written < 0 || (size_t)written >= sizeof(directory)) return false;
    DIR *dir = opendir(directory);
    if (!dir) return false;
    bool ok = true; struct dirent *entry;
    for (;;) {
        errno = 0; entry = readdir(dir);
        if (!entry) { if (errno) ok = false; break; }
        if (fs->cancel && fs->cancel(fs->user)) { ok = false; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            strncmp(entry->d_name, r->prefix+base, strlen(r->prefix+base))) continue;
        if (entry->d_name[0] == '.' && r->prefix[base] != '.') continue;
        written = snprintf(full, sizeof(full), "%s%s", directory, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(full)) { ok = false; break; }
        bool is_dir;
#ifdef DT_UNKNOWN
        if (entry->d_type != DT_UNKNOWN) is_dir = entry->d_type == DT_DIR;
        else
#endif
        {
            struct stat st;
            if (stat(full, &st)) { ok = false; break; }
            is_dir = S_ISDIR(st.st_mode);
        }
        if (r->kind == SOLAR_OS_COMPLETE_DIRECTORY && !is_dir) continue;
        written = snprintf(candidate, sizeof(candidate), "%.*s%s%s", (int)base,
                           r->prefix, entry->d_name, is_dir ? "/" : "");
        if (written < 0 || (size_t)written >= sizeof(candidate)) { ok = false; break; }
        if (!emit(sink, candidate)) { ok = false; break; }
    }
    closedir(dir); return ok;
}
