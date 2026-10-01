#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SOLAR_OS_COMPLETION_TOKEN_MAX 256
#define SOLAR_OS_COMPLETION_DISPLAY_MAX 20

typedef enum {
    SOLAR_OS_COMPLETE_FILE, SOLAR_OS_COMPLETE_DIRECTORY, SOLAR_OS_COMPLETE_COMMAND,
    SOLAR_OS_COMPLETE_APP, SOLAR_OS_COMPLETE_SESSION, SOLAR_OS_COMPLETE_JOB,
    SOLAR_OS_COMPLETE_SETTING, SOLAR_OS_COMPLETE_CUSTOM, SOLAR_OS_COMPLETE_COUNT
} solar_os_completion_kind_t;
typedef struct {
    const char *line;
    size_t cursor, start, end, argument;
    solar_os_completion_kind_t kind;
    char prefix[SOLAR_OS_COMPLETION_TOKEN_MAX];
} solar_os_completion_request_t;
/* Candidates are decoded tokens (directories include trailing '/'). The sink
 * consumes them immediately; neither providers nor the engine retain pointers. */
typedef bool (*solar_os_completion_emit_t)(void *, const char *candidate);
typedef bool (*solar_os_completion_provider_t)(const solar_os_completion_request_t *,
                                               solar_os_completion_emit_t, void *sink,
                                               void *user);
typedef struct {
    solar_os_completion_provider_t enumerate;
    void *user;
} solar_os_completion_provider_entry_t;
typedef struct {
    solar_os_completion_provider_entry_t providers[SOLAR_OS_COMPLETE_COUNT];
} solar_os_completion_registry_t;
typedef struct {
    size_t matches, displayed;
    bool changed, failed, overflow;
} solar_os_completion_result_t;
/* Parse only the argument containing cursor. Replacement consumes its old suffix
 * while preserving all other arguments. Unfinished quotes are accepted. */
bool solar_os_completion_request(const char *line, size_t cursor,
                                  solar_os_completion_request_t *request);
/* No index, candidate list, static state or heap allocation. display is called
 * at most DISPLAY_MAX times on a repeated Tab. Providers must return false on
 * incomplete enumeration: partial results are never inserted. */
solar_os_completion_result_t solar_os_completion_apply(
    char *line, size_t capacity, size_t *cursor, solar_os_completion_kind_t kind,
    const solar_os_completion_registry_t *registry, bool repeated_tab,
    solar_os_completion_emit_t display, void *display_user);
/* Raw single-field variant for file dialogs: spaces are literal, replacement
 * covers the field, and no shell quoting/trailing separator is inserted. */
solar_os_completion_result_t solar_os_completion_apply_field(
    char *text, size_t capacity, size_t *cursor, solar_os_completion_kind_t kind,
    const solar_os_completion_registry_t *registry, bool repeated_tab,
    solar_os_completion_emit_t display, void *display_user);
/* Reusable filesystem provider. cwd must be absolute; cancellation/yield callback
 * is optional. Only the requested directory is opened, with bounded scratch RAM. */
typedef struct {
    const char *cwd;
    bool (*cancel)(void *);
    void *user;
} solar_os_completion_files_t;
bool solar_os_completion_files(const solar_os_completion_request_t *,
                                solar_os_completion_emit_t, void *, void *);
#ifdef __cplusplus
}
#endif
