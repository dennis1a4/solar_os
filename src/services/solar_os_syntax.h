#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SOLAR_OS_SYNTAX_NONE,
    SOLAR_OS_SYNTAX_PYTHON,
    SOLAR_OS_SYNTAX_LUA,
} solar_os_syntax_language_t;

typedef enum {
    SOLAR_OS_SYNTAX_STYLE_NORMAL,
    SOLAR_OS_SYNTAX_STYLE_KEYWORD,
    SOLAR_OS_SYNTAX_STYLE_COMMENT,
    SOLAR_OS_SYNTAX_STYLE_STRING,
    SOLAR_OS_SYNTAX_STYLE_NUMBER,
    SOLAR_OS_SYNTAX_STYLE_BUILTIN,
    SOLAR_OS_SYNTAX_STYLE_DEFINITION,
    SOLAR_OS_SYNTAX_STYLE_CONSTANT,
} solar_os_syntax_style_t;

typedef enum {
    SOLAR_OS_SYNTAX_MODE_NORMAL,
    SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_SINGLE,
    SOLAR_OS_SYNTAX_MODE_PY_TRIPLE_DOUBLE,
    SOLAR_OS_SYNTAX_MODE_LUA_LONG_STRING,
    SOLAR_OS_SYNTAX_MODE_LUA_LONG_COMMENT,
} solar_os_syntax_mode_t;

typedef struct {
    solar_os_syntax_mode_t mode;
    uint8_t lua_long_equals;
} solar_os_syntax_state_t;

solar_os_syntax_language_t solar_os_syntax_language_for_path(const char *path);
void solar_os_syntax_state_init(solar_os_syntax_state_t *state);
void solar_os_syntax_highlight_line(solar_os_syntax_language_t language,
                                    solar_os_syntax_state_t *state,
                                    const char *line,
                                    size_t line_len,
                                    size_t visible_offset,
                                    uint8_t *styles,
                                    size_t visible_len);

/* Compact line-start states. Storage is supplied by the caller (three bytes
 * per line); no token tree or per-character file-wide index is retained. */
typedef struct { uint8_t mode, equals, known; } solar_os_syntax_checkpoint_t;
typedef struct {
    solar_os_syntax_checkpoint_t *lines;
    size_t capacity, count, valid, dirty_through;
    size_t lexed_lines; /* cumulative work counter for profiling/tests */
} solar_os_syntax_cache_t;
size_t solar_os_syntax_line_count(const char *text, size_t len);
void solar_os_syntax_cache_init(solar_os_syntax_cache_t *cache,
    solar_os_syntax_checkpoint_t *lines, size_t capacity, const char *text, size_t len);
/* Called BEFORE a buffer splice. Returns zero-based first affected line. */
size_t solar_os_syntax_cache_edit(solar_os_syntax_cache_t *cache,
    const char *text, size_t len, size_t start, size_t removed,
    const char *inserted, size_t added);
void solar_os_syntax_cache_step(solar_os_syntax_cache_t *cache,
    solar_os_syntax_language_t language, const char *text, size_t len,
    size_t through_line, size_t line_budget, size_t byte_budget);
int solar_os_syntax_cache_state(const solar_os_syntax_cache_t *cache,
    size_t line, solar_os_syntax_state_t *state);
