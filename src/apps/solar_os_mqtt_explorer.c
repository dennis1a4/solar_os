#include "solar_os_mqtt_explorer.h"
#include "solar_os_keys.h"
#include "solar_os_memory.h"
#include "solar_os_mqtt_capture.h"
#include "solar_os_storage.h"
#include "solar_os_tui.h"
#include "solar_os_tui_widgets.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EX_ROWS 256
#define EX_LINE 192
#define EX_DETAIL 16384

typedef struct {
    solar_os_tui_t tui;
    solar_os_mqtt_capture_t *capture;
    solar_os_mqtt_capture_config_t config;
    solar_os_mqtt_capture_status_t status;
    solar_os_mqtt_capture_message_t selected, scratch;
    char rows[EX_ROWS][EX_LINE], detail[EX_DETAIL], line[EX_LINE];
    uint16_t nodes[EX_ROWS], order[EX_ROWS], visit[EX_ROWS], depths[EX_ROWS];
    uint64_t sequences[EX_ROWS], chosen_sequence, log_next, log_lost;
    bool collapsed[MQTT_CAPTURE_TOPICS], visible[MQTT_CAPTURE_TOPICS];
    size_t count, cursor, top, detail_top;
    int chosen_node;
    uint64_t received, evicted, truncated, unindexed;
    unsigned topics;
    bool history, paused, input, hex, json, follow;
    char filter[64], edit[64], notice[96];
    FILE *log;
    uint32_t next_render;
} explorer_t;
static void *explorer_state;
#define ex (*(explorer_t *)explorer_state)

// Broker data never reaches the terminal as control sequences.
static void safe_text(char *out, size_t capacity, const char *s) {
    size_t n = 0;
    while (*s && n + 1 < capacity) {
        unsigned char c = *s++;
        out[n++] = (c >= 32 && c < 127) ? c : '.';
    }
    out[n] = 0;
}
static void line(size_t row, size_t col, size_t width, const char *text, uint8_t attr) {
    size_t n = strlen(text);
    if (n > width)
        n = width;
    if (n >= sizeof(ex.line))
        n = sizeof(ex.line) - 1;
    memcpy(ex.line, text, n);
    ex.line[n] = 0;
    solar_os_tui_addstr(&ex.tui, row, col, ex.line, attr);
}
static bool match(const char *s) { return !ex.filter[0] || strstr(s, ex.filter) != NULL; }
static void tree_rows(const solar_os_mqtt_capture_model_t *m, int unused, unsigned ignored) {
    (void)unused;
    (void)ignored;
    size_t pending = 0;
    for (unsigned k = m->topic_count; k > 0; --k) {
        unsigned id = ex.order[k - 1];
        if (m->topics[id].parent < 0 && ex.visible[id]) {
            ex.visit[pending] = id;
            ex.depths[pending++] = 0;
        }
    }
    while (pending) {
        unsigned id = ex.visit[--pending], depth = ex.depths[pending];
        const solar_os_mqtt_capture_topic_t *t = &m->topics[id];
        size_t row = ex.count++;
        ex.nodes[row] = id;
        bool children = false;
        for (unsigned j = 0; j < m->topic_count; ++j)
            if (m->topics[j].parent == (int)id) {
                children = true;
                break;
            }
        const char *label = t->path;
        if (t->parent >= 0)
            label += strlen(m->topics[t->parent].path) + 1;
        char clean[128];
        safe_text(clean, sizeof(clean), *label ? label : "(empty)");
        unsigned indent = depth > 8 ? 16 : depth * 2;
        char preview[25];
        size_t preview_len = t->latest.stored_size < 24 ? t->latest.stored_size : 24;
        for (size_t i = 0; i < preview_len; ++i) {
            unsigned char c = t->latest.payload[i];
            preview[i] = (c >= 32 && c < 127) ? c : '.';
        }
        preview[preview_len] = 0;
        if (t->count)
            snprintf(ex.rows[row], EX_LINE, "%*s%c %s [%" PRIu64 "] %s", indent, "",
                     children ? (ex.collapsed[id] ? '+' : '-') : ' ', clean, t->count, preview);
        else
            snprintf(ex.rows[row], EX_LINE, "%*s%c %s", indent, "",
                     children ? (ex.collapsed[id] ? '+' : '-') : ' ', clean);
        if (!ex.collapsed[id] || ex.filter[0])
            for (unsigned k = m->topic_count; k > 0; --k) {
                unsigned child = ex.order[k - 1];
                if (m->topics[child].parent == (int)id && ex.visible[child]) {
                    ex.visit[pending] = child;
                    ex.depths[pending++] = depth + 1;
                }
            }
    }
}
static void snapshot(void) {
    solar_os_mqtt_capture_model_t *m = solar_os_mqtt_capture_lock(ex.capture, &ex.status);
    ex.received = m->received;
    ex.evicted = m->evicted;
    ex.truncated = m->truncated;
    ex.unindexed = m->unindexed;
    ex.topics = m->topic_count;
    ex.count = 0;
    if (ex.history) {
        for (uint64_t seq = m->received; seq && m->received - seq < MQTT_CAPTURE_HISTORY; --seq) {
            const solar_os_mqtt_capture_message_t *msg = solar_os_mqtt_capture_find(m, seq);
            if (!msg || !match(msg->topic))
                continue;
            size_t row = ex.count++;
            ex.sequences[row] = seq;
            char topic[120];
            safe_text(topic, sizeof(topic), msg->topic);
            snprintf(ex.rows[row], EX_LINE, "%" PRIu64 " %s", seq, topic);
        }
        if (ex.follow && ex.count)
            ex.chosen_sequence = ex.sequences[0];
        for (size_t i = 0; i < ex.count; ++i)
            if (ex.sequences[i] == ex.chosen_sequence)
                ex.cursor = i;
    } else {
        memset(ex.visible, 0, sizeof(ex.visible));
        for (unsigned i = 0; i < m->topic_count; ++i) {
            ex.order[i] = i;
            if (match(m->topics[i].path))
                for (int j = (int)i; j >= 0; j = m->topics[j].parent)
                    ex.visible[j] = true;
        }
        for (unsigned i = 1; i < m->topic_count; ++i) {
            uint16_t id = ex.order[i];
            unsigned j = i;
            while (j && strcmp(m->topics[ex.order[j - 1]].path, m->topics[id].path) > 0) {
                ex.order[j] = ex.order[j - 1];
                --j;
            }
            ex.order[j] = id;
        }
        tree_rows(m, -1, 0);
        for (size_t i = 0; i < ex.count; ++i)
            if (ex.nodes[i] == ex.chosen_node)
                ex.cursor = i;
    }
    if (ex.cursor >= ex.count)
        ex.cursor = ex.count ? ex.count - 1 : 0;
    memset(&ex.selected, 0, sizeof(ex.selected));
    if (ex.count) {
        if (ex.history) {
            ex.chosen_sequence = ex.sequences[ex.cursor];
            const solar_os_mqtt_capture_message_t *msg =
                solar_os_mqtt_capture_find(m, ex.chosen_sequence);
            if (msg)
                ex.selected = *msg;
        } else {
            ex.chosen_node = ex.nodes[ex.cursor];
            ex.selected = m->topics[ex.chosen_node].latest;
        }
    }
    solar_os_mqtt_capture_unlock(ex.capture);
}
static void append(char c, size_t *at) {
    if (*at + 1 < sizeof(ex.detail))
        ex.detail[(*at)++] = c;
}
static void detail_text(void) {
    size_t at = 0;
    unsigned indent = 0;
    bool quoted = false, escaped = false;
    const solar_os_mqtt_capture_message_t *m = &ex.selected;
    if (ex.hex) {
        for (size_t i = 0; i < m->stored_size; i += 8) {
            char part[80];
            int n = snprintf(part, sizeof(part), "%04x  ", (unsigned)i);
            for (size_t j = 0; j < 8; ++j)
                n += snprintf(part + n, sizeof(part) - (size_t)n,
                              j + i < m->stored_size ? "%02x " : "   ",
                              j + i < m->stored_size ? m->payload[i + j] : 0);
            n += snprintf(part + n, sizeof(part) - (size_t)n, " ");
            for (size_t j = 0; j < 8 && i + j < m->stored_size; ++j) {
                uint8_t c = m->payload[i + j];
                part[n++] = (c >= 32 && c < 127) ? c : '.';
            }
            part[n++] = '\n';
            for (int j = 0; j < n; ++j)
                append(part[j], &at);
        }
    } else
        for (size_t i = 0; i < m->stored_size; ++i) {
            unsigned char c = m->payload[i];
            if (ex.json && !quoted && (c == ' ' || c == '\r' || c == '\n' || c == '\t'))
                continue;
            if (ex.json && !quoted && (c == '}' || c == ']')) {
                if (indent)
                    --indent;
                append('\n', &at);
                for (unsigned j = 0; j < indent * 2 && j < 32; ++j)
                    append(' ', &at);
            }
            if (c == '\n' || (c >= 32 && c < 127))
                append((char)c, &at);
            else {
                char escaped_byte[5];
                snprintf(escaped_byte, sizeof(escaped_byte), "\\x%02x", c);
                for (unsigned j = 0; j < 4; ++j)
                    append(escaped_byte[j], &at);
            }
            if (ex.json && !quoted && (c == '{' || c == '[' || c == ',')) {
                if (c != ',')
                    ++indent;
                append('\n', &at);
                for (unsigned j = 0; j < indent * 2 && j < 32; ++j)
                    append(' ', &at);
            }
            if (c == '"' && !escaped)
                quoted = !quoted;
            if (c == '\\' && !escaped)
                escaped = true;
            else
                escaped = false;
        }
    ex.detail[at] = 0;
}
static void render(void) {
    if (!ex.capture)
        return;
    snapshot();
    detail_text();
    size_t rows = solar_os_tui_rows(&ex.tui), cols = solar_os_tui_cols(&ex.tui);
    if (rows < 12 || cols < 48) {
        solar_os_tui_clear(&ex.tui);
        line(0, 0, cols, "MQTT Explorer needs at least 48x12", 0);
        solar_os_tui_refresh(&ex.tui);
        return;
    }
    size_t left = cols * 2 / 5, body = rows - 6, right = cols - left - 2;
    solar_os_tui_clear(&ex.tui);
    char text[EX_LINE];
    snprintf(text, sizeof(text), "MQTT Explorer | %s:%u | %s%s", ex.config.host, ex.config.port,
             ex.status.connected ? "CONNECTED" : "OFFLINE",
             ex.paused ? " | PAUSED (capture continues)" : "");
    line(0, 0, cols, text, SOLAR_OS_TUI_ATTR_INVERSE);
    snprintf(text, sizeof(text),
             "rx=%" PRIu64 " nodes=%u evicted=%" PRIu64 " truncated=%" PRIu64 " unindexed=%" PRIu64
             " gaps=%lu",
             ex.received, ex.topics, ex.evicted, ex.truncated, ex.unindexed,
             (unsigned long)ex.status.interruptions);
    line(1, 0, cols, text, 0);
    line(2, 0, left, ex.history ? "MESSAGE HISTORY" : "TOPICS [count] latest value",
         SOLAR_OS_TUI_ATTR_BOLD);
    line(2, left + 2, right,
         ex.hex    ? "PAYLOAD: HEX"
         : ex.json ? "PAYLOAD: JSON FORMAT"
                   : "PAYLOAD: TEXT",
         SOLAR_OS_TUI_ATTR_BOLD);
    if (ex.cursor < ex.top)
        ex.top = ex.cursor;
    if (ex.cursor >= ex.top + body)
        ex.top = ex.cursor - body + 1;
    for (size_t i = 0; i < body && ex.top + i < ex.count; ++i)
        line(3 + i, 0, left, ex.rows[ex.top + i],
             ex.top + i == ex.cursor ? SOLAR_OS_TUI_ATTR_INVERSE : 0);
    solar_os_tui_vline(&ex.tui, 2, left + 1, rows - 5, '|', 0);
    if (ex.selected.sequence) {
        char topic[MQTT_CAPTURE_TOPIC_BYTES];
        safe_text(topic, sizeof(topic), ex.selected.topic);
        line(3, left + 2, right, topic, 0);
        snprintf(text, sizeof(text), "#%" PRIu64 " t=%lums %luB QoS%u%s%s%s", ex.selected.sequence,
                 (unsigned long)ex.selected.received_ms, (unsigned long)ex.selected.payload_size,
                 ex.selected.qos, ex.selected.retained ? " RETAIN" : "",
                 ex.selected.duplicate ? " DUP" : "", ex.selected.truncated ? " TRUNC" : "");
        line(4, left + 2, right, text, 0);
        size_t row = 0, out = 0;
        char chunk[EX_LINE];
        for (const char *p = ex.detail;; ++p) {
            if (*p && *p != '\n' && out < right && out + 1 < sizeof(chunk)) {
                chunk[out++] = *p;
                continue;
            }
            chunk[out] = 0;
            if (row >= ex.detail_top && row - ex.detail_top < body - 2)
                line(5 + row - ex.detail_top, left + 2, right, chunk, 0);
            ++row;
            out = 0;
            if (!*p)
                break;
            if (*p != '\n')
                --p;
        }
    } else
        line(4, left + 2, right,
             ex.count ? "Select a topic with a value" : "Waiting for messages...", 0);
    snprintf(text, sizeof(text), "%s%s%s | log-lost=%" PRIu64,
             ex.notice[0] ? ex.notice : ex.status.detail, ex.log ? " | SD logging" : "",
             ex.filter[0] ? " | filtered" : "", ex.log_lost);
    line(rows - 3, 0, cols, text, 0);
    snprintf(text, sizeof(text), "%s%s",
             ex.input ? "Filter: " : "Filter (/ to edit): ", ex.input ? ex.edit : ex.filter);
    line(rows - 2, 0, cols, text, 0);
    line(rows - 1, 0, cols,
         "Tab tree/log  arrows select/collapse  Space pause  x hex  j JSON  [ ] scroll  / filter  "
         "q quit",
         SOLAR_OS_TUI_ATTR_INVERSE);
    solar_os_tui_set_cursor_visible(&ex.tui, false);
    solar_os_tui_refresh(&ex.tui);
}
static bool log_message(FILE *f, const solar_os_mqtt_capture_message_t *m) {
    if (fprintf(f, "{\"seq\":%" PRIu64 ",\"uptime_ms\":%lu,\"topic\":\"", m->sequence,
                (unsigned long)m->received_ms) < 0)
        return false;
    for (const unsigned char *p = (const unsigned char *)m->topic; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            if (fputc('\\', f) == EOF)
                return false;
        }
        if (*p < 32 || *p >= 127) {
            if (fprintf(f, "\\u%04x", *p) < 0)
                return false;
        } else if (fputc(*p, f) == EOF)
            return false;
    }
    if (fputs("\",\"topic_hex\":\"", f) < 0)
        return false;
    for (const unsigned char *p = (const unsigned char *)m->topic; *p; ++p)
        if (fprintf(f, "%02x", *p) < 0)
            return false;
    if (fprintf(f,
                "\",\"topic_bytes\":%u,\"payload_bytes\":%lu,\"qos\":%u,\"retain\":%s,"
                "\"duplicate\":%s,\"truncated\":%s,\"payload_hex\":\"",
                m->topic_size, (unsigned long)m->payload_size, m->qos,
                m->retained ? "true" : "false", m->duplicate ? "true" : "false",
                m->truncated ? "true" : "false") < 0)
        return false;
    for (unsigned i = 0; i < m->stored_size; ++i)
        if (fprintf(f, "%02x", m->payload[i]) < 0)
            return false;
    return fputs("\"}\n", f) >= 0;
}
static void log_pending(void) {
    if (!ex.log)
        return;
    for (unsigned i = 0; i < 16; ++i) {
        solar_os_mqtt_capture_model_t *m = solar_os_mqtt_capture_lock(ex.capture, NULL);
        uint64_t oldest =
            m->received >= MQTT_CAPTURE_HISTORY ? m->received - MQTT_CAPTURE_HISTORY + 1 : 1;
        if (ex.log_next < oldest) {
            ex.log_lost += oldest - ex.log_next;
            ex.log_next = oldest;
        }
        const solar_os_mqtt_capture_message_t *msg = solar_os_mqtt_capture_find(m, ex.log_next);
        if (msg)
            ex.scratch = *msg;
        solar_os_mqtt_capture_unlock(ex.capture);
        if (!msg)
            break;
        if (!log_message(ex.log, &ex.scratch) || fflush(ex.log)) {
            fclose(ex.log);
            ex.log = NULL;
            snprintf(ex.notice, sizeof(ex.notice), "Log write failed; capture continues");
            break;
        }
        ++ex.log_next;
    }
}
static bool credentials(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(ex.config.username, sizeof(ex.config.username), f) != NULL;
    if (ok && !strchr(ex.config.username, '\n') && !feof(f))
        ok = false;
    if (ok && !fgets(ex.config.password, sizeof(ex.config.password), f))
        ex.config.password[0] = 0;
    if (ok && !strchr(ex.config.password, '\n') && !feof(f))
        ok = false;
    ex.config.username[strcspn(ex.config.username, "\r\n")] = 0;
    ex.config.password[strcspn(ex.config.password, "\r\n")] = 0;
    fclose(f);
    return ok;
}
static esp_err_t start(solar_os_context_t *ctx) {
    int argc = solar_os_context_argc(ctx);
    const char *auth = NULL, *log = NULL;
    ex.config.port = 1883;
    ex.chosen_node = -1;
    ex.follow = true;
    ex.log_next = 1;
    if (argc < 2)
        goto usage;
    const char *host = solar_os_context_argv(ctx, 1);
    if (!strncmp(host, "mqtt://", 7))
        host += 7;
    if (strstr(host, "://") || strpbrk(host, "/@ \r\n") || strlen(host) >= sizeof(ex.config.host))
        goto usage;
    strcpy(ex.config.host, host);
    char *colon = strchr(ex.config.host, ':');
    if (colon) {
        char *end;
        *colon++ = 0;
        unsigned long port = strtoul(colon, &end, 10);
        if (!*colon || *end || !port || port > 65535)
            goto usage;
        ex.config.port = port;
    }
    if (!ex.config.host[0])
        goto usage;
    for (int i = 2; i < argc; ++i) {
        const char *opt = solar_os_context_argv(ctx, i);
        if (i + 1 >= argc)
            goto usage;
        if (!strcmp(opt, "--auth"))
            auth = solar_os_context_argv(ctx, ++i);
        else if (!strcmp(opt, "--log"))
            log = solar_os_context_argv(ctx, ++i);
        else
            goto usage;
    }
    if (auth && !credentials(auth)) {
        solar_os_context_finish(ctx, 1,
                                "mqttx: cannot read auth file (username and password lines)");
        return ESP_OK;
    }
    if (log) {
        ex.log = fopen(log, "wx");
        if (!ex.log) {
            solar_os_context_finish(ctx, 1, "mqttx: cannot create log; choose a new writable path");
            return ESP_OK;
        }
    }
    esp_err_t err = solar_os_mqtt_capture_start(&ex.config, &ex.capture);
    memset(ex.config.password, 0, sizeof(ex.config.password));
    if (err != ESP_OK) {
        solar_os_context_finish(ctx, 1, "mqttx: cannot start capture (memory or worker busy)");
        return ESP_OK;
    }
    err = solar_os_tui_screen_begin(&ex.tui, ctx);
    if (err != ESP_OK)
        return err;
    render();
    return ESP_OK;
usage:
    solar_os_context_finish(
        ctx, 2, "usage: mqttx HOST[:PORT] [--auth FILE] [--log NEWFILE] (MQTT 3.1.1 over TCP)");
    return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
    (void)ctx;
    if (ex.capture) {
        solar_os_mqtt_capture_halt(ex.capture);
        while (ex.log) {
            uint64_t before = ex.log_next;
            log_pending();
            if (before == ex.log_next)
                break;
        }
        if (ex.log)
            fprintf(ex.log, "{\"capture_end\":true,\"log_lost\":%" PRIu64 "}\n", ex.log_lost);
        solar_os_mqtt_capture_stop(ex.capture);
        ex.capture = NULL;
    }
    if (ex.log) {
        fclose(ex.log);
        ex.log = NULL;
    }
    memset(ex.config.password, 0, sizeof(ex.config.password));
    solar_os_tui_end(&ex.tui);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *e) {
    if (e->type == SOLAR_OS_EVENT_TICK) {
        log_pending();
        if (!ex.paused && !ex.input && (int32_t)(e->data.tick_ms - ex.next_render) >= 0) {
            ex.next_render = e->data.tick_ms + 250;
            render();
        }
        return true;
    }
    if (e->type != SOLAR_OS_EVENT_CHAR)
        return false;
    uint8_t key = (uint8_t)e->data.ch;
    if (key == SOLAR_OS_KEY_APP_EXIT) {
        solar_os_context_finish(ctx, 0, NULL);
        return true;
    }
    if (ex.input) {
        size_t n = strlen(ex.edit);
        if (key == SOLAR_OS_KEY_ESCAPE)
            ex.input = false;
        else if (key == '\r' || key == '\n') {
            strcpy(ex.filter, ex.edit);
            ex.input = false;
            ex.cursor = ex.top = 0;
            ex.chosen_node = -1;
            ex.chosen_sequence = 0;
        } else if (key == 8 || key == 127) {
            if (n)
                ex.edit[n - 1] = 0;
        } else if (key >= 32 && key < 127 && n + 1 < sizeof(ex.edit)) {
            ex.edit[n] = key;
            ex.edit[n + 1] = 0;
        }
        render();
        return true;
    }
    switch (key) {
    case 'q':
    case SOLAR_OS_KEY_ESCAPE:
        solar_os_context_finish(ctx, 0, NULL);
        return true;
    case ' ':
        ex.paused = !ex.paused;
        break;
    case '\t':
        ex.history = !ex.history;
        ex.cursor = ex.top = ex.detail_top = 0;
        ex.follow = true;
        break;
    case '/':
        ex.input = true;
        strcpy(ex.edit, ex.filter);
        break;
    case 'c':
        ex.filter[0] = 0;
        ex.cursor = ex.top = 0;
        ex.chosen_node = -1;
        ex.follow = true;
        break;
    case 'x':
        ex.hex = !ex.hex;
        ex.json = false;
        ex.detail_top = 0;
        break;
    case 'j':
        ex.json = !ex.json;
        ex.hex = false;
        ex.detail_top = 0;
        break;
    case '[':
        if (ex.detail_top)
            ex.detail_top -= 1;
        break;
    case ']':
        if (ex.detail_top < 4096)
            ++ex.detail_top;
        break;
    case SOLAR_OS_KEY_LEFT:
        if (!ex.history && ex.chosen_node >= 0)
            ex.collapsed[ex.chosen_node] = true;
        break;
    case SOLAR_OS_KEY_RIGHT:
        if (!ex.history && ex.chosen_node >= 0)
            ex.collapsed[ex.chosen_node] = false;
        break;
    case SOLAR_OS_KEY_HOME:
        ex.cursor = 0;
        ex.follow = true;
        goto choose;
    case SOLAR_OS_KEY_END:
        if (ex.count)
            ex.cursor = ex.count - 1;
        ex.follow = false;
        goto choose;
    case SOLAR_OS_KEY_UP:
        if (ex.cursor)
            --ex.cursor;
        ex.follow = false;
        goto choose;
    case SOLAR_OS_KEY_DOWN:
        if (ex.cursor + 1 < ex.count)
            ++ex.cursor;
        ex.follow = false;
        goto choose;
    default:
        return true;
    }
    render();
    return true;
choose:
    if (ex.count) {
        if (ex.history)
            ex.chosen_sequence = ex.sequences[ex.cursor];
        else
            ex.chosen_node = ex.nodes[ex.cursor];
    }
    ex.detail_top = 0;
    render();
    return true;
}
const solar_os_app_t solar_os_mqtt_explorer_app = {
    .name = "mqttx",
    .summary = "MQTT topic and message explorer",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .start = start,
    .stop = stop,
    .event = event,
    .tick_interval_ms = 50,
    .state_slot = &explorer_state,
    .state_size = sizeof(explorer_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
};
