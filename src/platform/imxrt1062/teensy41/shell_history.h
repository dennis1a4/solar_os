// Included by the shared shell, after its bounded RAM history helper.
// Flash history is separate for the three interactive consoles; script sessions
// keep ID zero and never read or write interactive history.
extern int sk_history_replace(const char *, const char *);
static const char *shell_history_path(unsigned id) {
    switch (id) {
    case 1: return "/flash/.shell/history-lcd";
    case 2: return "/flash/.shell/history-usb";
    case 3: return "/flash/.shell/history-telnet";
    default: return NULL;
    }
}
void solar_os_shell_history_store(solar_os_shell_session_t *session, unsigned id) {
    if (session) session->history_store = shell_history_path(id) ? id : 0;
}
bool solar_os_shell_history_flush(solar_os_shell_session_t *session, bool force) {
    if (!session || !session->history_dirty) return true;
    const uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
    if (!force && (uint32_t)(now - session->history_saved_ms) < 30000U) return true;
    // Rate-limit retries too, including a full or unavailable flash device.
    session->history_saved_ms = now;
    const char *path = shell_history_path(session->history_store);
    if (!path) return true;
    if (solar_os_storage_mkdir("/flash/.shell") != ESP_OK && errno != EEXIST) return false;
    char temp[64];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *file = fopen(temp, "w");
    if (!file) return false;
    bool ok = true;
    for (size_t i = 0; i < session->history_count && ok; ++i) {
        for (const char *p = session->history[i]; *p && ok; ++p)
            if (*p != '\r' && *p != '\n') ok = fputc((unsigned char)*p, file) != EOF;
        if (ok) ok = fputc('\n', file) != EOF;
    }
    if (fclose(file) != 0) ok = false;
    if (ok) ok = sk_history_replace(temp, path) == ESP_OK;
    if (ok) session->history_dirty = false;
    return ok;
}
static void shell_history_load(solar_os_shell_session_t *session) {
    const char *path = shell_history_path(session->history_store);
    if (!path) return;
    session->history_saved_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    FILE *file = fopen(path, "r");
    if (!file) return;
    char line[SHELL_INPUT_MAX + 1];
    // Bounded file and line scan. Never turn an incomplete record into a command.
    unsigned bytes = 0;
    while (bytes < SHELL_HISTORY_LEN * SHELL_INPUT_MAX && fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        bytes += length;
        if (!length || line[length - 1] != '\n') break;
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) < SHELL_INPUT_MAX) shell_history_add_ram(session, line);
    }
    fclose(file);
}
