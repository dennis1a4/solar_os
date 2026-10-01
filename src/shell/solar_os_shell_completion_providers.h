#pragma once
#include "solar_os.h"
#include "solar_os_completion.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Build a caller-owned registry; FILE/DIRECTORY use caller-owned filesystem
 * context. The context and registry must outlive the completion call. CUSTOM
 * remains available for the app to assign. No global registration or heap. */
void solar_os_shell_completion_providers(solar_os_context_t *,
    solar_os_completion_registry_t *, solar_os_completion_files_t *);
/* Platform session/job enumerator, called under the console's normal ownership. */
bool solar_os_shell_completion_runtime(const solar_os_completion_request_t *,
    solar_os_completion_emit_t, void *, void *);
#ifdef __cplusplus
}
#endif
