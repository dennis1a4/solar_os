#include "solar_os_shell_commands.h"
#include "solar_os_shell_common.h"
#include "solar_os_shell_io.h"
#include "solar_os_mag.h"
#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void solar_os_shell_cmd_mag(solar_os_context_t *ctx, int argc, char **argv)
{
    solar_os_shell_io_t *io = solar_os_shell_command_io(ctx);
    solar_os_mag_info_t info;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0)) {
        if (solar_os_mag_count() == 0) {
            solar_os_shell_io_writeln(io, "no magnetometers registered");
        }
        for (size_t i = 0; solar_os_mag_get(i, &info); ++i) {
            solar_os_shell_io_printf(io, "%s  %s field-uT\n", info.name, info.driver);
        }
        return;
    }
    if (argc < 2 || argc > 4 || strcmp(argv[1], "sample") != 0) {
        solar_os_shell_io_writeln(io, "usage: mag [list] | mag sample [name] [timeout-ms]");
        return;
    }
    const char *name = argc >= 3 ? argv[2] :
        solar_os_mag_get(0, &info) ? info.name : NULL;
    char *end = NULL;
    errno = 0;
    unsigned long timeout = argc == 4 ? strtoul(argv[3], &end, 10) : 1000;
    if (!name || errno == ERANGE || timeout > UINT32_MAX ||
        (argc == 4 && (!argv[3][0] || argv[3][0] == '-' || *end))) {
        solar_os_shell_io_writeln(io, "mag: no sensor or invalid timeout");
        return;
    }
    solar_os_mag_sample_t sample;
    esp_err_t result = solar_os_mag_read_sample(name, (uint32_t)timeout, &sample);
    if (result != ESP_OK) {
        solar_os_shell_io_printf(io, "mag: %s\n", esp_err_to_name(result));
        return;
    }
    solar_os_shell_io_printf(io, "%s timestamp-us=%" PRIu64 "\n", name, sample.timestamp_us);
    if (sample.valid & SOLAR_OS_MAG_CAP_FIELD) {
        solar_os_shell_io_printf(io, "field-uT x=%.6f y=%.6f z=%.6f\n",
            (double)sample.magnetic_field_ut[0], (double)sample.magnetic_field_ut[1],
            (double)sample.magnetic_field_ut[2]);
    }
}
