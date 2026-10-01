#include "solar_os_completion.h"
#include <ctype.h>
#include <string.h>

bool solar_os_completion_request(const char *line, size_t cursor,
                                  solar_os_completion_request_t *r)
{
    if (!line || !r || cursor > strlen(line)) return false;
    memset(r, 0, sizeof(*r)); r->line = line; r->cursor = cursor;
    size_t pos = 0, argument = 0;
    for (;;) {
        while (pos < cursor && isspace((unsigned char)line[pos])) ++pos;
        r->start = pos; r->argument = argument;
        size_t used = 0; char quote = 0;
        while (line[pos]) {
            char ch = line[pos];
            if (!quote && isspace((unsigned char)ch)) break;
            if ((ch == '\'' || ch == '"') && (!quote || ch == quote)) {
                quote = quote ? 0 : ch; ++pos; continue;
            }
            size_t source = pos;
            if (ch == '\\' && quote != '\'' && line[pos+1]) ch = line[++pos];
            ++pos;
            if (source < cursor) {
                if (used + 1 >= sizeof(r->prefix)) return false;
                r->prefix[used++] = ch;
            }
        }
        if (pos >= cursor) { r->end = pos; r->prefix[used] = 0; return true; }
        ++argument;
    }
}

typedef struct {
    const solar_os_completion_request_t *request;
    solar_os_completion_result_t result;
    char common[SOLAR_OS_COMPLETION_TOKEN_MAX];
    solar_os_completion_emit_t display;
    void *user;
} completion_sink_t;
static bool collect(void *user, const char *candidate)
{
    completion_sink_t *s = user;
    if (!candidate || strncmp(candidate, s->request->prefix, strlen(s->request->prefix))) return true;
    size_t n = strlen(candidate);
    if (n >= sizeof(s->common)) { s->result.overflow = true; return true; }
    for (size_t i = 0; i < n; ++i) if (iscntrl((unsigned char)candidate[i])) return true;
    if (!s->result.matches) memcpy(s->common, candidate, n+1);
    else {
        size_t i = 0;
        while (s->common[i] && s->common[i] == candidate[i]) ++i;
        s->common[i] = 0;
    }
    ++s->result.matches;
    if (s->display && s->result.displayed < SOLAR_OS_COMPLETION_DISPLAY_MAX) {
        if (!s->display(s->user, candidate)) return false;
        ++s->result.displayed;
    }
    return true;
}
/* Canonical double quoting keeps decoded spaces/quotes/backslashes intact. */
static bool encode(const char *text, char *out, size_t cap)
{
    bool quote = !*text;
    for (const char *p = text; *p; ++p)
        if (isspace((unsigned char)*p) || strchr("'\"\\|<>&;", *p)) quote = true;
    size_t n = 0;
    if (quote) out[n++] = '"';
    for (; *text; ++text) {
        if (n + 3 >= cap) return false;
        if (quote && (*text == '\\' || *text == '"')) out[n++] = '\\';
        out[n++] = *text;
    }
    if (quote) out[n++] = '"';
    out[n] = 0; return true;
}
static solar_os_completion_result_t completion_apply(
    char *line, size_t cap, size_t *cursor, solar_os_completion_kind_t kind,
    const solar_os_completion_registry_t *registry, bool repeated,
    solar_os_completion_emit_t display, void *user, bool field)
{
    completion_sink_t s = {0}; solar_os_completion_request_t request;
    if (!line || !cursor || !registry || kind >= SOLAR_OS_COMPLETE_COUNT ||
        (int)kind < 0 || !cap || strnlen(line, cap) == cap ||
        !solar_os_completion_request(line, *cursor, &request)) { s.result.failed = true; return s.result; }
    if (field) {
        if (*cursor >= sizeof(request.prefix)) { s.result.overflow = true; return s.result; }
        request.start = request.argument = 0; request.end = strlen(line);
        memcpy(request.prefix, line, *cursor); request.prefix[*cursor] = 0;
    }
    request.kind = kind; s.request = &request;
    /* First pass determines uniqueness before showing anything. Second pass is
     * streaming and bounded in output, but still enumerates the directory only. */
    solar_os_completion_provider_entry_t provider = registry->providers[kind];
    if (!provider.enumerate) return s.result;
    if (!provider.enumerate(&request, collect, &s, provider.user)) { s.result.failed = true; return s.result; }
    if (s.result.overflow) return s.result;
    if (s.result.matches > 1 && repeated && display) {
        completion_sink_t listing = {0}; listing.request = &request;
        listing.display = display; listing.user = user;
        if (!provider.enumerate(&request, collect, &listing, provider.user)) s.result.failed = true;
        s.result.displayed = listing.result.displayed;
        return s.result;
    }
    if (!s.result.matches || (s.result.matches > 1 && strlen(s.common) <= strlen(request.prefix))) return s.result;
    char encoded[SOLAR_OS_COMPLETION_TOKEN_MAX * 2 + 3];
    if (field) strcpy(encoded, s.common);
    else if (!encode(s.common, encoded, sizeof(encoded))) { s.result.overflow = true; return s.result; }
    size_t n = strlen(encoded), total = strlen(line), common = strlen(s.common);
    bool directory = common && s.common[common-1] == '/';
    if (!field && s.result.matches == 1 && !directory && !line[request.end]) encoded[n++] = ' ';
    if (request.start + n + total - request.end >= cap) { s.result.overflow = true; return s.result; }
    memmove(line + request.start + n, line + request.end, total - request.end + 1);
    memcpy(line + request.start, encoded, n);
    *cursor = request.start + n;
    /* Keep cursor inside quotes after a directory slash, ready for its child. */
    if (!field && directory && n && encoded[n-1] == '"') --*cursor;
    s.result.changed = true; return s.result;
}

solar_os_completion_result_t solar_os_completion_apply(
    char *line, size_t capacity, size_t *cursor, solar_os_completion_kind_t kind,
    const solar_os_completion_registry_t *registry, bool repeated,
    solar_os_completion_emit_t display, void *user)
{
    return completion_apply(line,capacity,cursor,kind,registry,repeated,display,user,false);
}
solar_os_completion_result_t solar_os_completion_apply_field(
    char *text, size_t capacity, size_t *cursor, solar_os_completion_kind_t kind,
    const solar_os_completion_registry_t *registry, bool repeated,
    solar_os_completion_emit_t display, void *user)
{
    return completion_apply(text,capacity,cursor,kind,registry,repeated,display,user,true);
}
