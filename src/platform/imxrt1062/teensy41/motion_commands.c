#if SK_MOTION
#include "solar_os_shell_commands.h"
#include "solar_os_shell_common.h"
#include "solar_os_shell_io.h"
#include "solar_os_stream.h"
#include <string.h>

void sk_shell_cmd_motion_stream(solar_os_context_t *ctx, int argc, char **argv)
{
    solar_os_shell_io_t *io = solar_os_shell_command_io(ctx);
    solar_os_stream_info_t info;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0)) {
        for (size_t i = 0; solar_os_stream_get(i, &info); ++i) {
            solar_os_shell_io_printf(io, "%s %s %s %s\n", info.id, info.provider,
                solar_os_stream_type_name(info.type), info.format);
        }
        return;
    }
    if (argc != 3 || (strcmp(argv[1], "status") && strcmp(argv[1], "sample"))) {
        solar_os_shell_io_writeln(io, "usage: stream list | stream status ID | stream sample ID");
        return;
    }
    esp_err_t result = solar_os_stream_get_info(argv[2], &info);
    if (result != ESP_OK) {
        solar_os_shell_io_printf(io, "stream: %s\n", esp_err_to_name(result));
        return;
    }
    if (strcmp(argv[1], "status") == 0) {
        solar_os_shell_io_printf(io, "%s provider=%s device=%s format=%s unit=%s handles=%lu\n%s\n",
            info.id, info.provider, info.device, info.format, info.unit,
            (unsigned long)info.active_handles, info.summary);
        return;
    }
    solar_os_stream_handle_t handle = SOLAR_OS_STREAM_HANDLE_INIT;
    result = solar_os_stream_open(info.id, "sensor-shell", &handle);
    if (result != ESP_OK) {
        solar_os_shell_io_printf(io, "stream: %s\n", esp_err_to_name(result));
        return;
    }
    solar_os_stream_csv_record_t record;
    const solar_os_stream_read_options_t options = {.timeout_ms = 1000};
    result = solar_os_stream_read_csv(&handle, &options, &record);
    solar_os_stream_close(&handle);
    if (result != ESP_OK) {
        solar_os_shell_io_printf(io, "stream: %s\n", esp_err_to_name(result));
        return;
    }
    char header[SOLAR_OS_STREAM_CSV_HEADER_MAX];
    if (solar_os_stream_csv_header(&info, header, sizeof(header)) == ESP_OK) {
        solar_os_shell_io_writeln(io, header);
    }
    if (record.has_data) solar_os_shell_io_writeln(io, record.line);
}
#endif
